// 配置管理模块
const ConfigManager = {
    // 默认配置模板
    defaultConfigTemplate: {
        // 视频连接配置
        video: {
            signalingUrl: 'ws://fy403.cn:8000',
            remoteId: 'cam_dYFh3H3kf',
            iceServers: [
                {
                    urls: ['stun:stun.l.google.com:19302']
                },
                {
                    urls: ['turn:tx.fy403.cn:3478?transport=udp'],
                    username: 'fy403',
                    credential: 'qwertyuiop'
                }
            ],
            bundlePolicy: 'max-bundle',
            rtcpMuxPolicy: 'require',
            encodedInsertableStreams: false,
            // 视频参数配置
            videoConfig: {
                resolution: '640x480',
                fps: 30,
                bitrate: 8000000,
                format: 'yuyv422'
            }
        },
        // 数据连接配置
        data: {
            signalingUrl: 'ws://fy403.cn:8000',
            remoteId: 'data_Dd8fgkoKo90',
            iceServers: [
                {
                    urls: ['stun:stun.l.google.com:19302']
                },
                {
                    urls: ['turn:tx.fy403.cn:3478?transport=udp'],
                    username: 'fy403',
                    credential: 'qwertyuiop'
                }
            ]
        },
    // 通道按键绑定配置
    channelBindings: {
        // 1-16通道的按键绑定
        ch1: { type: 'continuous', negativeKey: 'KeyS', positiveKey: 'KeyW', minValue: -1.0, maxValue: 1.0, startValue: 0 },
        ch2: { type: 'continuous', negativeKey: 'KeyA', positiveKey: 'KeyD', minValue: -1.0, maxValue: 1.0, startValue: 0 },
        ch3: null,
        ch4: null,
        ch5: null,
        ch6: null,
        ch7: null,
        ch8: null,
        ch9: null,
        ch10: null,
        ch11: null,
        ch12: null,
        ch13: null,
        ch14: null,
        ch15: null,
        ch16: null
    },
    // 主题颜色配置
    theme: {
        primaryColor: '#FFA500',
        primaryBright: '#FFD700',
        primaryDim: '#CC8400',
        secondaryColor: '#00CED1',
        secondaryDim: '#008B8B'
    }
    },

    // 配置集合（多套配置）
    configs: {},
    // 当前使用的配置名称
    currentConfigName: 'default',

    // 获取所有配置列表
    getConfigList() {
        return Object.keys(this.configs);
    },

    // 获取当前配置名称
    getCurrentConfigName() {
        return this.currentConfigName;
    },

    // 初始化
    init() {
        this.loadConfigs();
        // 如果没有配置，创建默认配置
        if (Object.keys(this.configs).length === 0) {
            this.createConfig('default', '默认配置');
        }
        // 如果当前配置不存在，使用第一个配置
        if (!this.configs[this.currentConfigName]) {
            this.currentConfigName = Object.keys(this.configs)[0];
        }
        return this.currentConfig;
    },

    // 加载所有配置
    loadConfigs() {
        try {
            const configsJson = localStorage.getItem('webrtc_configs');
            if (configsJson) {
                this.configs = JSON.parse(configsJson);
            }
            const currentName = localStorage.getItem('webrtc_current_config');
            if (currentName) {
                this.currentConfigName = currentName;
            }
        } catch (e) {
            console.error('配置加载失败:', e);
            this.configs = {};
        }
    },

    // 保存所有配置
    saveConfigs() {
        try {
            localStorage.setItem('webrtc_configs', JSON.stringify(this.configs));
            localStorage.setItem('webrtc_current_config', this.currentConfigName);
            console.log('配置保存成功');
            return true;
        } catch (e) {
            console.error('配置保存失败:', e);
            return false;
        }
    },

    // 获取当前配置
    currentConfig: null,

    // 加载当前配置（兼容旧代码）
    loadConfig() {
        this.init();
        return this.currentConfig;
    },

    // 获取当前配置对象
    getCurrentConfig() {
        return this.configs[this.currentConfigName];
    },

    // 创建新配置
    createConfig(name, description = '') {
        if (!name || name.trim() === '') {
            throw new Error('配置名称不能为空');
        }
        if (this.configs[name]) {
            throw new Error('配置名称已存在');
        }
        const newConfig = JSON.parse(JSON.stringify(this.defaultConfigTemplate));
        newConfig.name = name;
        newConfig.description = description;
        newConfig.createdAt = new Date().toISOString();
        newConfig.updatedAt = new Date().toISOString();
        this.configs[name] = newConfig;
        this.saveConfigs();
        return newConfig;
    },

    // 更新配置
    updateConfig(name, configData) {
        if (!this.configs[name]) {
            throw new Error('配置不存在');
        }
        Object.assign(this.configs[name], configData);
        this.configs[name].updatedAt = new Date().toISOString();
        this.saveConfigs();
        return this.configs[name];
    },

    // 删除配置
    deleteConfig(name) {
        if (!this.configs[name]) {
            throw new Error('配置不存在');
        }
        if (Object.keys(this.configs).length === 1) {
            throw new Error('不能删除最后一个配置');
        }
        delete this.configs[name];
        // 如果删除的是当前配置，切换到第一个配置
        if (this.currentConfigName === name) {
            this.currentConfigName = Object.keys(this.configs)[0];
        }
        this.saveConfigs();
        return true;
    },

    // 切换配置
    switchConfig(name) {
        if (!this.configs[name]) {
            throw new Error('配置不存在');
        }
        this.currentConfigName = name;
        this.saveConfigs();
        return this.configs[name];
    },

    // 重命名配置
    renameConfig(oldName, newName) {
        if (!this.configs[oldName]) {
            throw new Error('原配置不存在');
        }
        if (this.configs[newName] && newName !== oldName) {
            throw new Error('新配置名称已存在');
        }
        if (newName.trim() === '') {
            throw new Error('配置名称不能为空');
        }
        const config = this.configs[oldName];
        config.name = newName;
        this.configs[newName] = config;
        if (oldName !== newName) {
            delete this.configs[oldName];
            if (this.currentConfigName === oldName) {
                this.currentConfigName = newName;
            }
        }
        this.saveConfigs();
        return config;
    },

    // 复制配置
    copyConfig(sourceName, newName) {
        if (!this.configs[sourceName]) {
            throw new Error('源配置不存在');
        }
        if (this.configs[newName]) {
            throw new Error('配置名称已存在');
        }
        const copiedConfig = JSON.parse(JSON.stringify(this.configs[sourceName]));
        copiedConfig.name = newName;
        copiedConfig.createdAt = new Date().toISOString();
        copiedConfig.updatedAt = new Date().toISOString();
        this.configs[newName] = copiedConfig;
        this.saveConfigs();
        return copiedConfig;
    },

    // 重置当前配置为默认值
    resetCurrentConfig() {
        const config = this.configs[this.currentConfigName];
        const defaultTemplate = JSON.parse(JSON.stringify(this.defaultConfigTemplate));
        config.video = defaultTemplate.video;
        config.data = defaultTemplate.data;
        config.updatedAt = new Date().toISOString();
        this.saveConfigs();
        return config;
    },

    // 与默认配置合并（兼容旧代码）
    mergeWithDefaults(config) {
        const merged = JSON.parse(JSON.stringify(this.defaultConfigTemplate));
        this.deepMerge(merged, config);
        return merged;
    },

    // 深度合并
    deepMerge(target, source) {
        for (const key in source) {
            if (source[key] instanceof Object && key in target) {
                this.deepMerge(target[key], source[key]);
            } else {
                target[key] = source[key];
            }
        }
    },

    // 重置配置为默认值（兼容旧代码）
    resetConfig() {
        return this.resetCurrentConfig();
    },

    // 保存配置（兼容旧代码）
    saveConfig() {
        return this.saveConfigs();
    },

    // 获取视频配置（兼容旧代码）
    getVideoConfig() {
        const config = this.getCurrentConfig();
        return config ? config.video : null;
    },

    // 获取数据配置（兼容旧代码）
    getDataConfig() {
        const config = this.getCurrentConfig();
        return config ? config.data : null;
    },

    // 更新视频配置
    updateVideoConfig(newConfig) {
        const config = this.getCurrentConfig();
        if (!config) {
            this.init();
        }
        Object.assign(this.getCurrentConfig().video, newConfig);
        this.getCurrentConfig().updatedAt = new Date().toISOString();
        return this.saveConfigs();
    },

    // 更新视频参数配置
    updateVideoParams(params) {
        const config = this.getCurrentConfig();
        if (!config) {
            this.init();
        }
        if (!config.video.videoConfig) {
            config.video.videoConfig = {};
        }
        Object.assign(config.video.videoConfig, params);
        config.updatedAt = new Date().toISOString();
        return this.saveConfigs();
    },

    // 获取视频参数配置
    getVideoParams() {
        const config = this.getCurrentConfig();
        return config && config.video.videoConfig ? config.video.videoConfig : null;
    },

    // 更新数据配置
    updateDataConfig(newConfig) {
        const config = this.getCurrentConfig();
        if (!config) {
            this.init();
        }
        Object.assign(this.getCurrentConfig().data, newConfig);
        this.getCurrentConfig().updatedAt = new Date().toISOString();
        return this.saveConfigs();
    },

    // 获取通道按键绑定
    getChannelBindings() {
        const config = this.getCurrentConfig();
        return config && config.channelBindings ? config.channelBindings : {};
    },

    // 更新通道按键绑定
    updateChannelBindings(bindings) {
        const config = this.getCurrentConfig();
        if (!config) {
            this.init();
        }
        if (!config.channelBindings) {
            config.channelBindings = {};
        }
        Object.assign(config.channelBindings, bindings);
        config.updatedAt = new Date().toISOString();
        return this.saveConfigs();
    },

    // 获取主题颜色配置
    getTheme() {
        const config = this.getCurrentConfig();
        return config && config.theme ? config.theme : this.defaultConfigTemplate.theme;
    },

    // 更新主题颜色配置
    updateTheme(themeData) {
        const config = this.getCurrentConfig();
        if (!config) {
            this.init();
        }
        if (!config.theme) {
            config.theme = {};
        }
        Object.assign(config.theme, themeData);
        config.updatedAt = new Date().toISOString();
        this.saveConfigs();
        this.applyTheme();
        return config.theme;
    },

    // 应用主题到CSS变量
    applyTheme() {
        const theme = this.getTheme();
        const root = document.documentElement;
        root.style.setProperty('--titan-primary', theme.primaryColor);
        root.style.setProperty('--titan-primary-bright', theme.primaryBright);
        root.style.setProperty('--titan-primary-dim', theme.primaryDim);
        root.style.setProperty('--titan-secondary', theme.secondaryColor);
        root.style.setProperty('--titan-secondary-dim', theme.secondaryDim);
        
        // 更新发光效果
        const primaryColor = this.hexToRgb(theme.primaryColor);
        root.style.setProperty('--titan-glow', `0 0 10px rgba(${primaryColor.r}, ${primaryColor.g}, ${primaryColor.b}, 0.5)`);
        root.style.setProperty('--titan-glow-strong', `0 0 20px rgba(${primaryColor.r}, ${primaryColor.g}, ${primaryColor.b}, 0.8)`);
    },

    // 将十六进制颜色转换为RGB
    hexToRgb(hex) {
        const result = /^#?([a-f\d]{2})([a-f\d]{2})([a-f\d]{2})$/i.exec(hex);
        return result ? {
            r: parseInt(result[1], 16),
            g: parseInt(result[2], 16),
            b: parseInt(result[3], 16)
        } : { r: 255, g: 165, b: 0 };
    },

    // 根据主色调自动生成亮色和暗色
    generateThemeColors(primaryColor) {
        const rgb = this.hexToRgb(primaryColor);
        const hsl = this.rgbToHsl(rgb.r, rgb.g, rgb.b);
        
        // 亮色：增加亮度
        const brightHsl = { h: hsl.h, s: hsl.s, l: Math.min(hsl.l + 0.15, 1) };
        const brightRgb = this.hslToRgb(brightHsl.h, brightHsl.s, brightHsl.l);
        const primaryBright = this.rgbToHex(brightRgb.r, brightRgb.g, brightRgb.b);
        
        // 暗色：减少亮度
        const dimHsl = { h: hsl.h, s: hsl.s, l: Math.max(hsl.l - 0.15, 0) };
        const dimRgb = this.hslToRgb(dimHsl.h, dimHsl.s, dimHsl.l);
        const primaryDim = this.rgbToHex(dimRgb.r, dimRgb.g, dimRgb.b);
        
        return {
            primaryColor,
            primaryBright,
            primaryDim,
            secondaryColor: '#00CED1',
            secondaryDim: '#008B8B'
        };
    },

    // RGB转HSL
    rgbToHsl(r, g, b) {
        r /= 255;
        g /= 255;
        b /= 255;
        const max = Math.max(r, g, b);
        const min = Math.min(r, g, b);
        let h, s, l = (max + min) / 2;

        if (max === min) {
            h = s = 0;
        } else {
            const d = max - min;
            s = l > 0.5 ? d / (2 - max - min) : d / (max + min);
            switch (max) {
                case r: h = ((g - b) / d + (g < b ? 6 : 0)) / 6; break;
                case g: h = ((b - r) / d + 2) / 6; break;
                case b: h = ((r - g) / d + 4) / 6; break;
            }
        }
        return { h, s, l };
    },

    // HSL转RGB
    hslToRgb(h, s, l) {
        let r, g, b;
        if (s === 0) {
            r = g = b = l;
        } else {
            const hue2rgb = (p, q, t) => {
                if (t < 0) t += 1;
                if (t > 1) t -= 1;
                if (t < 1/6) return p + (q - p) * 6 * t;
                if (t < 1/2) return q;
                if (t < 2/3) return p + (q - p) * (2/3 - t) * 6;
                return p;
            };
            const q = l < 0.5 ? l * (1 + s) : l + s - l * s;
            const p = 2 * l - q;
            r = hue2rgb(p, q, h + 1/3);
            g = hue2rgb(p, q, h);
            b = hue2rgb(p, q, h - 1/3);
        }
        return { r: Math.round(r * 255), g: Math.round(g * 255), b: Math.round(b * 255) };
    },

    // RGB转十六进制
    rgbToHex(r, g, b) {
        return '#' + [r, g, b].map(x => {
            const hex = x.toString(16);
            return hex.length === 1 ? '0' + hex : hex;
        }).join('');
    }
};

// 页面加载时初始化配置
ConfigManager.init();
// 应用主题颜色
ConfigManager.applyTheme();
