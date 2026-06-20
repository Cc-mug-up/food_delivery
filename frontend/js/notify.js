// ============================================================
// 通知模块 — 桌面通知 + Toast + 弹窗
// ============================================================

const Notify = {
    // 请求桌面通知权限
    async requestPermission() {
        if (!('Notification' in window)) {
            console.log('Browser does not support Notification');
            return;
        }
        if (Notification.permission === 'default') {
            await Notification.requestPermission();
        }
    },

    // 发送桌面通知
    sendDesktop(title, body) {
        if (!('Notification' in window)) return;
        if (Notification.permission === 'granted') {
            new Notification(title, {
                body: body,
                icon: 'data:image/svg+xml,<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><text y=".9em" font-size="90">🛵</text></svg>',
                tag: 'food-delivery',
                requireInteraction: false,
            });
        }
    },

    // 显示订单弹窗
    showOrderPopup(orderNo, info) {
        const popup = document.getElementById('order-popup');
        document.getElementById('popup-order-no').textContent = orderNo;
        document.getElementById('popup-info').textContent = info;
        popup.classList.remove('hidden');

        // 3秒后自动关闭
        setTimeout(() => {
            popup.classList.add('hidden');
        }, 4000);

        // 点击关闭
        popup.onclick = () => popup.classList.add('hidden');
    },

    // Toast 消息
    showToast(message, duration = 3000) {
        const container = document.getElementById('toast-container');
        const toast = document.createElement('div');
        toast.className = 'toast';
        toast.textContent = message;
        container.appendChild(toast);

        setTimeout(() => {
            toast.style.opacity = '0';
            toast.style.transition = 'opacity 0.3s';
            setTimeout(() => toast.remove(), 300);
        }, duration);
    },

    // 播放提示音 (使用 Web Audio API)
    playSound() {
        try {
            const ctx = new (window.AudioContext || window.webkitAudioContext)();
            const osc = ctx.createOscillator();
            const gain = ctx.createGain();
            osc.connect(gain);
            gain.connect(ctx.destination);
            osc.frequency.setValueAtTime(800, ctx.currentTime);
            osc.frequency.setValueAtTime(1200, ctx.currentTime + 0.1);
            gain.gain.setValueAtTime(0.3, ctx.currentTime);
            gain.gain.exponentialRampToValueAtTime(0.01, ctx.currentTime + 0.3);
            osc.start(ctx.currentTime);
            osc.stop(ctx.currentTime + 0.3);
        } catch (e) { /* ignore */ }
    }
};
