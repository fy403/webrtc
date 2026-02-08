// 配置管理模块
const ConfigManager = {
    // 默认配置
    defaultConfig: {
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
            encodedInsertableStreams: false
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
        }
    },

    // 当前配置
    currentConfig: null,

    // 加载配置
    loadConfig() {
        try {
            const configJson = localStorage.getItem('webrtc_config');
            if (configJson) {
                const config = JSON.parse(configJson);
                this.currentConfig = this.mergeWithDefaults(config);
                console.log('配置加载成功:', this.currentConfig);
                return this.currentConfig;
            }
        } catch (e) {
            console.error('配置加载失败:', e);
        }
        // 使用默认配置
        this.currentConfig = JSON.parse(JSON.stringify(this.defaultConfig));
        this.saveConfig();
        return this.currentConfig;
    },

    // 保存配置
    saveConfig() {
        try {
            localStorage.setItem('webrtc_config', JSON.stringify(this.currentConfig));
            console.log('配置保存成功');
            return true;
        } catch (e) {
            console.error('配置保存失败:', e);
            return false;
        }
    },

    // 与默认配置合并
    mergeWithDefaults(config) {
        const merged = JSON.parse(JSON.stringify(this.defaultConfig));
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

    // 重置配置为默认值
    resetConfig() {
        this.currentConfig = JSON.parse(JSON.stringify(this.defaultConfig));
        this.saveConfig();
        return this.currentConfig;
    },

    // 获取视频配置
    getVideoConfig() {
        if (!this.currentConfig) {
            this.loadConfig();
        }
        return this.currentConfig.video;
    },

    // 获取数据配置
    getDataConfig() {
        if (!this.currentConfig) {
            this.loadConfig();
        }
        return this.currentConfig.data;
    },

    // 更新视频配置
    updateVideoConfig(newConfig) {
        Object.assign(this.currentConfig.video, newConfig);
        return this.saveConfig();
    },

    // 更新数据配置
    updateDataConfig(newConfig) {
        Object.assign(this.currentConfig.data, newConfig);
        return this.saveConfig();
    }
};

// 页面加载时初始化配置
ConfigManager.loadConfig();
