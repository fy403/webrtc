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

    // 更新数据配置
    updateDataConfig(newConfig) {
        const config = this.getCurrentConfig();
        if (!config) {
            this.init();
        }
        Object.assign(this.getCurrentConfig().data, newConfig);
        this.getCurrentConfig().updatedAt = new Date().toISOString();
        return this.saveConfigs();
    }
};

// 页面加载时初始化配置
ConfigManager.init();
