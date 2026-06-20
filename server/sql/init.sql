-- ============================================================
-- 外卖系统 - 数据库初始化脚本
-- ============================================================

CREATE DATABASE IF NOT EXISTS food_delivery
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE food_delivery;

-- ----------------------------
-- 1. 菜单表
-- ----------------------------
CREATE TABLE IF NOT EXISTS menu_items (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    name        VARCHAR(100)   NOT NULL,
    category    VARCHAR(50)    NOT NULL DEFAULT '其他',
    price       DECIMAL(10, 2) NOT NULL CHECK (price > 0),
    image       VARCHAR(255)   DEFAULT '',
    description VARCHAR(500)   DEFAULT '',
    is_available TINYINT(1)    NOT NULL DEFAULT 1,
    created_at  DATETIME       NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at  DATETIME       NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_category (category),
    INDEX idx_available (is_available)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- 2. 订单表
-- ----------------------------
CREATE TABLE IF NOT EXISTS orders (
    id              INT AUTO_INCREMENT PRIMARY KEY,
    order_no        VARCHAR(32)    NOT NULL UNIQUE COMMENT '订单编号, 如 OD20240620153001',
    customer_name   VARCHAR(50)    NOT NULL,
    phone           VARCHAR(20)    NOT NULL,
    address         VARCHAR(255)   NOT NULL,
    total_price     DECIMAL(10, 2) NOT NULL CHECK (total_price >= 0),
    status          ENUM('pending','confirmed','preparing','delivering','delivered','cancelled')
                        NOT NULL DEFAULT 'pending',
    remark          VARCHAR(500)   DEFAULT '',
    created_at      DATETIME       NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME       NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_order_no (order_no),
    INDEX idx_status (status),
    INDEX idx_created_at (created_at),
    INDEX idx_phone (phone)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- 3. 订单明细表
-- ----------------------------
CREATE TABLE IF NOT EXISTS order_items (
    id           INT AUTO_INCREMENT PRIMARY KEY,
    order_id     INT            NOT NULL,
    menu_item_id INT            NOT NULL,
    quantity     INT            NOT NULL CHECK (quantity > 0),
    unit_price   DECIMAL(10, 2) NOT NULL CHECK (unit_price > 0),
    FOREIGN KEY (order_id)     REFERENCES orders(id)     ON DELETE CASCADE,
    FOREIGN KEY (menu_item_id) REFERENCES menu_items(id) ON DELETE RESTRICT,
    INDEX idx_order_id (order_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- 4. 订单状态变更日志
-- ----------------------------
CREATE TABLE IF NOT EXISTS order_status_log (
    id          INT AUTO_INCREMENT PRIMARY KEY,
    order_id    INT          NOT NULL,
    from_status VARCHAR(20)  NOT NULL DEFAULT '',
    to_status   VARCHAR(20)  NOT NULL,
    created_at  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (order_id) REFERENCES orders(id) ON DELETE CASCADE,
    INDEX idx_order_id (order_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- 5. 管理员用户表
-- ----------------------------
CREATE TABLE IF NOT EXISTS users (
    id            INT AUTO_INCREMENT PRIMARY KEY,
    username      VARCHAR(50)  NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    role          ENUM('admin','staff') NOT NULL DEFAULT 'staff',
    created_at    DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

-- ----------------------------
-- 示例菜单数据
-- ----------------------------
INSERT INTO menu_items (name, category, price, image, description) VALUES
('宫保鸡丁',   '热菜', 28.00, 'gongbao.jpg',   '经典川菜, 鸡肉丁与花生米爆炒'),
('鱼香肉丝',   '热菜', 25.00, 'yuxiang.jpg',   '猪肉丝配木耳胡萝卜, 酸甜微辣'),
('麻婆豆腐',   '热菜', 18.00, 'mapo.jpg',      '嫩豆腐配麻辣肉末, 川味十足'),
('糖醋里脊',   '热菜', 30.00, 'tangcu.jpg',    '外酥里嫩, 酸甜可口'),
('红烧排骨',   '热菜', 35.00, 'paigu.jpg',     '慢炖排骨, 酱香浓郁'),
('番茄炒蛋',   '热菜', 15.00, 'fanqie.jpg',    '家常经典, 酸甜滑嫩'),
('蛋炒饭',     '主食', 12.00, 'chaofan.jpg',   '粒粒分明, 蛋香四溢'),
('牛肉拉面',   '主食', 20.00, 'lamian.jpg',    '手工拉面, 牛肉汤底浓郁'),
('饺子(猪肉)', '主食', 18.00, 'jiaozi.jpg',    '皮薄馅大, 配醋更佳'),
('珍珠奶茶',   '饮品', 12.00, 'naicha.jpg',    'Q弹珍珠配香浓奶茶'),
('冰镇柠檬水', '饮品', 8.00,  'ningmeng.jpg',  '清爽解腻, 夏季必备'),
('可乐',       '饮品', 5.00,  'cola.jpg',      '冰镇碳酸饮料');

-- 插入默认管理员 (密码: admin123 的简单哈希)
INSERT INTO users (username, password_hash, role) VALUES
('admin', '8c6976e5b5410415bde908bd4dee15dfb167a9c873fc4bb8a81f6f2ab448a918', 'admin');
