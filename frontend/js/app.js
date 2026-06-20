// ============================================================
// 外卖系统前端主逻辑 — 菜单, 购物车, 下单, SSE 实时推送, 订单大屏
// ============================================================

const API = {
    base: '',
    async get(path)   { const r = await fetch(this.base + path); return r.json(); },
    async post(path, data) {
        const r = await fetch(this.base + path, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        return r.json();
    },
    async put(path, data) {
        const r = await fetch(this.base + path, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(data)
        });
        return r.json();
    }
};

// ---- 全局状态 ----
let menuItems = [];
let cart = {};       // { menu_id: { name, price, qty } }
let sseSource = null;

// ---- 初始化 ----
document.addEventListener('DOMContentLoaded', async () => {
    updateClock();
    setInterval(updateClock, 1000);
    await Notify.requestPermission();
    await loadMenu();
    await loadStats();
    await loadOrders();
    connectSSE();
    setupForm();
});

function updateClock() {
    const now = new Date();
    document.getElementById('clock').textContent =
        now.toLocaleTimeString('zh-CN', { hour12: false });
}

// ============================================================
// 菜单加载
// ============================================================
async function loadMenu() {
    const container = document.getElementById('menu-container');
    try {
        const resp = await API.get('/api/menu');
        menuItems = resp.items || [];
        renderMenu();
    } catch (e) {
        container.innerHTML = '<div class="loading">菜单加载失败</div>';
    }
}

function renderMenu() {
    const container = document.getElementById('menu-container');
    container.innerHTML = menuItems.map(item => `
        <div class="menu-item ${cart[item.id] ? 'selected' : ''}"
             data-id="${item.id}" onclick="toggleMenuItem(this, ${item.id})">
            <div class="name">${escHtml(item.name)}</div>
            <div class="cat">${escHtml(item.category)}</div>
            <div class="price">¥${item.price.toFixed(2)}</div>
            <div class="qty-ctrl">
                <button onclick="event.stopPropagation(); changeQty(${item.id}, -1)">−</button>
                <span class="qty">${(cart[item.id] || {}).qty || 0}</span>
                <button onclick="event.stopPropagation(); changeQty(${item.id}, 1)">+</button>
            </div>
        </div>
    `).join('');
}

function toggleMenuItem(el, id) {
    if (cart[id]) {
        delete cart[id];
    } else {
        const item = menuItems.find(m => m.id === id);
        if (item) cart[id] = { name: item.name, price: item.price, qty: 1 };
    }
    renderMenu();
    renderCart();
}

function changeQty(id, delta) {
    if (!cart[id]) return;
    cart[id].qty += delta;
    if (cart[id].qty <= 0) {
        delete cart[id];
        renderMenu();
    }
    renderCart();
    // 更新菜单中的数量显示
    const el = document.querySelector(`.menu-item[data-id="${id}"] .qty`);
    if (el && cart[id]) el.textContent = cart[id].qty;
}

function renderCart() {
    const container = document.getElementById('cart-items');
    const entries = Object.entries(cart);
    if (entries.length === 0) {
        container.innerHTML = '<p class="hint">请从菜单中添加菜品</p>';
        document.getElementById('cart-total').textContent = '¥0.00';
        return;
    }
    let total = 0;
    container.innerHTML = entries.map(([id, item]) => {
        const subtotal = item.price * item.qty;
        total += subtotal;
        return `<div class="cart-item">
            <span>${escHtml(item.name)} ×${item.qty}</span>
            <span>¥${subtotal.toFixed(2)}
                <button class="remove-btn" onclick="delete cart[${id}]; renderMenu(); renderCart();">×</button>
            </span>
        </div>`;
    }).join('');
    document.getElementById('cart-total').textContent = '¥' + total.toFixed(2);
}

// ============================================================
// 下单
// ============================================================
function setupForm() {
    document.getElementById('order-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        const cartEntries = Object.entries(cart);
        if (cartEntries.length === 0) {
            Notify.showToast('请先选择菜品');
            return;
        }

        const name  = document.getElementById('cust-name').value.trim();
        const phone = document.getElementById('cust-phone').value.trim();
        const addr  = document.getElementById('cust-addr').value.trim();
        const remark = document.getElementById('cust-remark').value.trim();

        if (!name || !phone || !addr) {
            Notify.showToast('请填写完整信息');
            return;
        }

        if (!/^1[3-9]\d{9}$/.test(phone)) {
            Notify.showToast('手机号格式不正确');
            return;
        }

        const btn = document.getElementById('submit-btn');
        btn.disabled = true;
        btn.textContent = '提交中...';

        try {
            const resp = await API.post('/api/orders', {
                customer_name: name,
                phone: phone,
                address: addr,
                remark: remark,
                items: cartEntries.map(([id, item]) => ({
                    menu_item_id: parseInt(id),
                    quantity: item.qty
                }))
            });

            if (resp.order_no) {
                Notify.showToast(`下单成功! 订单号: ${resp.order_no}`);
                // 清空购物车
                cart = {};
                renderMenu();
                renderCart();
                document.getElementById('order-form').reset();
            } else {
                Notify.showToast('下单失败: ' + (resp.error || '未知错误'));
            }
        } catch (err) {
            Notify.showToast('网络错误, 请稍后重试');
        }

        btn.disabled = false;
        btn.textContent = '立即下单 🚀';
    });
}

// ============================================================
// SSE 实时推送
// ============================================================
function connectSSE() {
    if (sseSource) { sseSource.close(); }

    const statusEl = document.getElementById('sse-status');
    sseSource = new EventSource('/api/events');

    sseSource.onopen = () => {
        statusEl.textContent = '● 推送在线';
        statusEl.className = 'badge badge-connected';
        console.log('SSE connected');
    };

    sseSource.onerror = () => {
        statusEl.textContent = '● 推送离线';
        statusEl.className = 'badge badge-disconnected';
        // EventSource 会自动重连
    };

    // 新订单事件
    sseSource.addEventListener('new_order', (e) => {
        try {
            const data = JSON.parse(e.data);
            handleNewOrder(data);
        } catch (err) {
            console.error('SSE parse error:', err);
        }
    });

    // 订单状态变更事件
    sseSource.addEventListener('order_status', (e) => {
        try {
            const data = JSON.parse(e.data);
            handleStatusUpdate(data);
        } catch (err) {
            console.error('SSE parse error:', err);
        }
    });
}

function handleNewOrder(data) {
    // 大屏显示
    addToBigScreen(data);

    // 弹窗 (大号字体显示订单号)
    const info = `${data.customer_name} · ¥${data.total_price.toFixed(2)}`;
    Notify.showOrderPopup(data.order_no, info);

    // 桌面通知
    Notify.sendDesktop('🛎️ 新订单!', `${data.order_no} — ${data.customer_name}`);

    // 提示音
    Notify.playSound();

    // 刷新订单列表和统计
    loadOrders();
    loadStats();
}

function handleStatusUpdate(data) {
    Notify.showToast(`订单 ${data.order_no}: ${data.old_status} → ${data.new_status}`);
    loadOrders();
    loadStats();
}

// ============================================================
// 订单大屏
// ============================================================
const MAX_SCREEN_ITEMS = 8;

function addToBigScreen(data) {
    const container = document.getElementById('screen-content');
    // 移除placeholder
    const placeholder = container.querySelector('.screen-placeholder');
    if (placeholder) placeholder.remove();

    const div = document.createElement('div');
    div.className = 'screen-item';
    div.innerHTML = `
        <span class="s-order-no">${escHtml(data.order_no)}</span>
        <span class="s-info">${escHtml(data.customer_name)}</span>
        <span class="s-price">¥${data.total_price.toFixed(2)}</span>
    `;
    container.insertBefore(div, container.firstChild);

    // 限制显示数量
    while (container.children.length > MAX_SCREEN_ITEMS) {
        const last = container.lastElementChild;
        last.style.animation = 'fadeOut 0.3s ease-out';
        setTimeout(() => last.remove(), 300);
    }
}

// ============================================================
// 订单列表
// ============================================================
async function loadOrders() {
    const status = document.getElementById('filter-status').value;
    let url = '/api/orders?page_size=50';
    if (status) url += '&status=' + encodeURIComponent(status);

    const container = document.getElementById('order-list');
    try {
        const resp = await API.get(url);
        const orders = resp.orders || [];
        if (orders.length === 0) {
            container.innerHTML = '<p class="hint">暂无订单</p>';
            return;
        }
        container.innerHTML = orders.map(o => renderOrderCard(o)).join('');
    } catch (e) {
        container.innerHTML = '<p class="hint">加载失败</p>';
    }
}

function renderOrderCard(o) {
    const statusMap = {
        'pending': '待确认', 'confirmed': '已确认', 'preparing': '备餐中',
        'delivering': '配送中', 'delivered': '已送达', 'cancelled': '已取消'
    };
    const statusLabel = statusMap[o.status] || o.status;
    const canAdvance = ['pending','confirmed','preparing','delivering'].includes(o.status);
    const canCancel = ['pending','confirmed'].includes(o.status);

    let actionsHtml = '';
    if (canAdvance) {
        const nextStatus = getNextStatus(o.status);
        actionsHtml += `<button class="btn btn-sm" onclick="advanceStatus(${o.id}, '${nextStatus}')">
            ▸ ${getNextStatusLabel(o.status)}</button>`;
    }
    if (canCancel) {
        actionsHtml += `<button class="btn btn-sm" onclick="advanceStatus(${o.id}, 'cancelled')">
            ✕ 取消</button>`;
    }

    return `<div class="order-card" id="order-${o.id}">
        <div class="o-header">
            <span class="o-no">${escHtml(o.order_no)}</span>
            <span class="o-status status-${o.status}">${statusLabel}</span>
        </div>
        <div class="o-detail">
            <span>👤 ${escHtml(o.customer_name)}</span>
            <span>📱 ${escHtml(o.phone)}</span>
            <span>📍 ${escHtml(o.address)}</span>
        </div>
        <div class="o-detail" style="margin-top:4px;">
            <span class="o-price">¥${o.total_price.toFixed(2)}</span>
            <span style="font-size:12px;color:#666;">${o.created_at}</span>
        </div>
        <div class="o-actions">${actionsHtml}</div>
    </div>`;
}

function getNextStatus(current) {
    const flow = { 'pending': 'confirmed', 'confirmed': 'preparing', 'preparing': 'delivering', 'delivering': 'delivered' };
    return flow[current] || current;
}

function getNextStatusLabel(current) {
    const labels = { 'pending': '确认接单', 'confirmed': '开始备餐', 'preparing': '开始配送', 'delivering': '确认送达' };
    return labels[current] || '更新';
}

async function advanceStatus(orderId, newStatus) {
    try {
        const resp = await API.put(`/api/orders/${orderId}/status`, { status: newStatus });
        if (resp.order_no) {
            Notify.showToast(`订单 ${resp.order_no}: ${resp.old_status} → ${resp.new_status}`);
            loadOrders();
            loadStats();
        } else {
            Notify.showToast('操作失败: ' + (resp.error || '未知错误'));
        }
    } catch (e) {
        Notify.showToast('网络错误');
    }
}

// ---- 筛选刷新 ----
document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('filter-status').addEventListener('change', loadOrders);
    document.getElementById('refresh-btn').addEventListener('click', () => {
        loadOrders();
        loadStats();
    });
});

// ============================================================
// 统计
// ============================================================
async function loadStats() {
    try {
        const resp = await API.get('/api/stats');
        document.getElementById('stat-today').textContent = resp.today_orders || 0;
        document.getElementById('stat-revenue').textContent = '¥' + ((resp.today_revenue || 0).toFixed(0));
        const counts = resp.status_counts || {};
        document.getElementById('stat-pending').textContent = counts.pending || 0;
    } catch (e) { /* ignore */ }
}

// ============================================================
// 工具函数
// ============================================================
function escHtml(s) {
    const div = document.createElement('div');
    div.textContent = s;
    return div.innerHTML;
}
