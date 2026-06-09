// 通道按键绑定管理器
(function (global) {
  class ChannelKeyBinder {
    constructor() {
      this.bindings = {};
      this.channelValues = [1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500];
      this.keyPressTimestamps = {};   // keyCode -> timestamp when first pressed
      this.activeDirection = {};      // channelNum -> 1 or -1 (direction of active key)
      this.onValueChange = null;
      this.keydownHandler = null;
      this.keyupHandler = null;
      this._tick = this._tick.bind(this);
      this._running = false;
      this._previousValues = [...this.channelValues]; // for change detection
    }

    // 初始化绑定配置
    init(bindings) {
      this.bindings = bindings || {};

      // 迁移：CH1/CH2 为 null 时填入默认 WASD 绑定（CH1=前后W/S, CH2=左右A/D）
      const defaults = {
        ch1: { type: 'continuous', negativeKey: 'KeyS', positiveKey: 'KeyW', minValue: 1000, maxValue: 2000, startValue: 1500, neutralValue: 1500, invert: false, curveId: 'linear' },
        ch2: { type: 'continuous', negativeKey: 'KeyA', positiveKey: 'KeyD', minValue: 1000, maxValue: 2000, startValue: 1500, neutralValue: 1500, invert: false, curveId: 'linear' },
      };
      let migrated = false;
      for (const [key, def] of Object.entries(defaults)) {
        if (!this.bindings[key]) {
          this.bindings[key] = def;
          migrated = true;
        }
      }
      // 迁移旧格式：将 -1~1 百分比格式自动升级为 1000~2000 raw PWM
      let upgraded = false;
      for (let i = 1; i <= 16; i++) {
        const b = this.bindings[`ch${i}`];
        if (!b) continue;
        // 检测旧格式：maxValue <= 1.0 说明是 -1~1 百分比
        if (b.maxValue !== undefined && b.maxValue <= 1.0) {
          b.minValue = 1000;
          b.maxValue = 2000;
          b.startValue = 1500;
          if (b.neutralValue === undefined || b.neutralValue <= 0) b.neutralValue = 1500;
          if (b.type === 'single' && b.value !== undefined && b.value <= 1.0) b.value = 2000;
          upgraded = true;
        }
        // 补全缺失字段
        if (!b.curveId) b.curveId = 'linear';
        if (b.neutralValue === undefined) b.neutralValue = 1500;
        if (b.invert === undefined) b.invert = false;
        if (b.startValue === undefined) b.startValue = 1500;
      }
      if (upgraded) {
        console.log('[ChannelKeyBinder] Upgraded old -1~1 bindings to 1000~2000 raw PWM');
        try { ConfigManager.updateChannelBindings(this.bindings); } catch(e) {}
      }
      if (migrated) {
        console.log('[ChannelKeyBinder] Migrated CH1/CH2 to default WASD bindings');
        try { ConfigManager.updateChannelBindings(this.bindings); } catch(e) {}
      }

      this._buildCurveCache();
      console.log('通道按键绑定已初始化:', this.bindings);
    }

    // 为每条绑定预构建 SpeedCurve
    _buildCurveCache() {
      this._curveCache = {};
      for (let i = 1; i <= 16; i++) {
        const binding = this.bindings[`ch${i}`];
        if (!binding) continue;
        this._curveCache[i] = this._getCurveForChannel(i);
      }
    }

    // 获取通道对应的 SpeedCurve
    _getCurveForChannel(channelNum) {
      const binding = this.bindings[`ch${channelNum}`];
      if (!binding) return global.DEFAULT_SPEED_CURVE || new SpeedCurve([]);
      const curveId = binding.curveId;
      if (!curveId) return global.DEFAULT_SPEED_CURVE || new SpeedCurve([]);
      // 从 speedCurveManager 获取曲线
      if (global.speedCurveManager) {
        const curve = global.speedCurveManager.getCurve(curveId);
        if (curve) return new SpeedCurve(curve.points);
      }
      return global.DEFAULT_SPEED_CURVE || new SpeedCurve([]);
    }

    // 加载配置
    loadFromConfig() {
      const bindings = ConfigManager.getChannelBindings();
      this.init(bindings);
    }

    // 设置值变化回调
    setOnValueChange(callback) {
      this.onValueChange = callback;
    }

    // 启动键盘监听
    start() {
      if (this._running) return;

      this.keydownHandler = (e) => this._handleKeydown(e);
      this.keyupHandler = (e) => this._handleKeyup(e);

      window.addEventListener('keydown', this.keydownHandler);
      window.addEventListener('keyup', this.keyupHandler);

      this._running = true;
      requestAnimationFrame(this._tick);
    }

    // 停止键盘监听
    stop() {
      this._running = false;
      if (this.keydownHandler) {
        window.removeEventListener('keydown', this.keydownHandler);
        this.keydownHandler = null;
      }
      if (this.keyupHandler) {
        window.removeEventListener('keyup', this.keyupHandler);
        this.keyupHandler = null;
      }
    }

    // 处理键盘按下
    _handleKeydown(e) {
      const activeTag = document.activeElement?.tagName;
      if (activeTag === 'INPUT' || activeTag === 'TEXTAREA') return;

      const code = e.code;

      for (let i = 1; i <= 16; i++) {
        const binding = this.bindings[`ch${i}`];
        if (!binding) continue;

        if (binding.type === 'single') {
          if (binding.key === code && !this.keyPressTimestamps[code]) {
            e.preventDefault();
            this.keyPressTimestamps[code] = performance.now();
            this.activeDirection[i] = 1;
            return;
          }
        } else if (binding.type === 'continuous') {
          if (binding.negativeKey === code && !this.keyPressTimestamps[code]) {
            e.preventDefault();
            this.keyPressTimestamps[code] = performance.now();
            this.activeDirection[i] = -1;
            return;
          }
          if (binding.positiveKey === code && !this.keyPressTimestamps[code]) {
            e.preventDefault();
            this.keyPressTimestamps[code] = performance.now();
            this.activeDirection[i] = 1;
            return;
          }
        }
      }
    }

    // 处理键盘抬起
    _handleKeyup(e) {
      const code = e.code;

      for (let i = 1; i <= 16; i++) {
        const binding = this.bindings[`ch${i}`];
        if (!binding) continue;

        if (binding.type === 'single') {
          if (binding.key === code) {
            e.preventDefault();
            delete this.keyPressTimestamps[code];
            delete this.activeDirection[i];
            // 值由 tick 衰减到 0
            return;
          }
        }

        if (binding.type === 'continuous') {
          if (binding.negativeKey === code || binding.positiveKey === code) {
            delete this.keyPressTimestamps[code];
            // 检查该通道是否还有其他键被按下
            const negStillDown = this.keyPressTimestamps[binding.negativeKey] !== undefined;
            const posStillDown = this.keyPressTimestamps[binding.positiveKey] !== undefined;

            if (!negStillDown && !posStillDown) {
              delete this.activeDirection[i];
            } else if (negStillDown) {
              this.activeDirection[i] = -1;
            } else if (posStillDown) {
              this.activeDirection[i] = 1;
            }
            return;
          }
        }
      }
    }

    // 每帧 tick：根据按键时长 + 曲线计算并更新通道值
    _tick() {
      const now = performance.now();
      let changed = false;

      for (let i = 1; i <= 16; i++) {
        const binding = this.bindings[`ch${i}`];
        if (!binding) {
          // 无绑定的通道回归默认中位 1500
          if (Math.abs(this.channelValues[i - 1] - 1500) > 0.5) {
            const diff = 1500 - this.channelValues[i - 1];
            const step = diff * 0.3;
            if (Math.abs(step) < 1) {
              this.channelValues[i - 1] = 1500;
            } else {
              this.channelValues[i - 1] += step;
            }
            changed = true;
          }
          continue;
        }

        const neutralValue = binding.neutralValue !== undefined ? binding.neutralValue : 1500;
        const direction = this.activeDirection[i];

        if (binding.type === 'single') {
          if (direction === 1) {
            // 一档模式：查找到该按键的时间戳
            let elapsed = 0;
            if (binding.key && this.keyPressTimestamps[binding.key]) {
              elapsed = now - this.keyPressTimestamps[binding.key];
            }
            const curve = this._curveCache[i] || global.DEFAULT_SPEED_CURVE;
            const factor = curve.sample(elapsed);
            const rawValue = binding.value;
            let targetVal = binding.value * factor + neutralValue * (1 - factor);
            // 应用 invert 翻转
            if (binding.invert) {
              targetVal = 2 * neutralValue - targetVal;
            }
            if (Math.abs(targetVal - this.channelValues[i - 1]) > 0.5) {
              this.channelValues[i - 1] = targetVal;
              changed = true;
            }
          } else {
            // 按键释放，衰减到 neutralValue
            if (Math.abs(this.channelValues[i - 1] - neutralValue) > 0.5) {
              const diff = neutralValue - this.channelValues[i - 1];
              const step = diff * 0.3;
              if (Math.abs(step) < 1) {
                this.channelValues[i - 1] = neutralValue;
              } else {
                this.channelValues[i - 1] += step;
              }
              changed = true;
            }
          }
        } else if (binding.type === 'continuous') {
          if (direction) {
            // 连续模式：计算按住时长，从曲线获取因子
            let elapsed = 0;
            const lookupKey = direction === 1 ? binding.positiveKey : binding.negativeKey;
            if (lookupKey && this.keyPressTimestamps[lookupKey]) {
              elapsed = now - this.keyPressTimestamps[lookupKey];
            }
            const curve = this._curveCache[i] || global.DEFAULT_SPEED_CURVE;
            const factor = curve.sample(elapsed); // 0..1

            const minValue = binding.minValue !== undefined ? binding.minValue : 1000;
            const maxValue = binding.maxValue !== undefined ? binding.maxValue : 2000;

            let targetVal;
            if (direction === -1) {
              targetVal = minValue * factor + neutralValue * (1 - factor);
            } else {
              targetVal = maxValue * factor + neutralValue * (1 - factor);
            }
            // 应用 invert 翻转
            if (binding.invert) {
              targetVal = 2 * neutralValue - targetVal;
            }
            if (Math.abs(targetVal - this.channelValues[i - 1]) > 0.5) {
              this.channelValues[i - 1] = targetVal;
              changed = true;
            }
          } else {
            // 没有按键，衰减到 neutralValue
            if (Math.abs(this.channelValues[i - 1] - neutralValue) > 0.5) {
              const diff = neutralValue - this.channelValues[i - 1];
              const step = diff * 0.3;
              if (Math.abs(step) < 1) {
                this.channelValues[i - 1] = neutralValue;
              } else {
                this.channelValues[i - 1] += step;
              }
              changed = true;
            }
          }
        }
      }

      if (changed) {
        this._notifyValueChange();
      }

      if (this._running) {
        requestAnimationFrame(this._tick);
      }
    }

    // 通知值变化
    _notifyValueChange() {
      window.currentChannelValues = [...this.channelValues];
      if (this.onValueChange) {
        this.onValueChange(this.channelValues);
      }
    }

    // 获取当前通道值
    getChannelValue(channelNum) {
      return this.channelValues[channelNum - 1];
    }

    // 获取所有通道值
    getAllChannelValues() {
      return [...this.channelValues];
    }

    // 检查按键冲突
    checkKeyConflict(channelNum, newBinding) {
      if (!newBinding) return null;

      const conflicts = [];
      const newKeys = [];

      if (newBinding.type === 'single') {
        if (newBinding.key) newKeys.push(newBinding.key);
      } else if (newBinding.type === 'continuous') {
        if (newBinding.negativeKey) newKeys.push(newBinding.negativeKey);
        if (newBinding.positiveKey) newKeys.push(newBinding.positiveKey);

        if (newBinding.negativeKey && newBinding.positiveKey && newBinding.negativeKey === newBinding.positiveKey) {
          conflicts.push({
            channel: channelNum,
            key: newBinding.negativeKey,
            type: 'internal',
            message: `连续模式的负值和正值不能设置为同一按键`
          });
        }
      }

      if (conflicts.length > 0) return conflicts[0];

      for (const newKey of newKeys) {
        for (let i = 1; i <= 16; i++) {
          if (i === channelNum) {
            const existingBinding = this.bindings[`ch${i}`];
            if (!existingBinding) continue;

            if (newBinding.type === 'continuous' && existingBinding.type === 'continuous') {
              // skip
            } else {
              const existingKeys = [];
              if (existingBinding.type === 'single' && existingBinding.key) {
                existingKeys.push(existingBinding.key);
              } else if (existingBinding.type === 'continuous') {
                if (existingBinding.negativeKey) existingKeys.push(existingBinding.negativeKey);
                if (existingBinding.positiveKey) existingKeys.push(existingBinding.positiveKey);
              }

              if (existingKeys.includes(newKey)) {
                conflicts.push({
                  channel: i,
                  key: newKey,
                  type: 'same-channel',
                  message: `按键 ${this.formatKeyName(newKey)} 在当前通道的其他模式中已使用`
                });
                break;
              }
            }
            continue;
          }

          const existingBinding = this.bindings[`ch${i}`];
          if (!existingBinding) continue;

          if (existingBinding.type === 'single' && existingBinding.key === newKey) {
            conflicts.push({
              channel: i,
              key: newKey,
              type: 'single',
              message: `按键 ${this.formatKeyName(newKey)} 已被通道${i}的一档模式占用`
            });
            break;
          }

          if (existingBinding.type === 'continuous') {
            if (existingBinding.negativeKey === newKey) {
              conflicts.push({
                channel: i,
                key: newKey,
                type: 'continuous-negative',
                message: `按键 ${this.formatKeyName(newKey)} 已被通道${i}的连续模式负值占用`
              });
              break;
            }
            if (existingBinding.positiveKey === newKey) {
              conflicts.push({
                channel: i,
                key: newKey,
                type: 'continuous-positive',
                message: `按键 ${this.formatKeyName(newKey)} 已被通道${i}的连续模式正值占用`
              });
              break;
            }
          }
        }

        if (conflicts.length > 0) break;
      }

      return conflicts.length > 0 ? conflicts[0] : null;
    }

    // 格式化按键名
    formatKeyName(code) {
      if (!code) return '';
      return code.replace('Key', '').replace('Digit', '');
    }

    // 设置通道绑定
    setBinding(channelNum, binding) {
      const conflict = this.checkKeyConflict(channelNum, binding);
      if (conflict) {
        console.warn(`按键冲突: ${conflict.message}`);
        alert(`按键冲突: ${conflict.message}`);
        return false;
      }

      this.bindings[`ch${channelNum}`] = binding;
      // 重建该通道的曲线缓存
      this._curveCache[channelNum] = this._getCurveForChannel(channelNum);
      // 保存到配置
      ConfigManager.updateChannelBindings({ [`ch${channelNum}`]: binding });
      return true;
    }

    // 获取通道绑定
    getBinding(channelNum) {
      return this.bindings[`ch${channelNum}`];
    }

    // 清除通道绑定
    clearBinding(channelNum) {
      this.bindings[`ch${channelNum}`] = null;
      delete this._curveCache[channelNum];
      delete this.activeDirection[channelNum];
      this.channelValues[channelNum - 1] = 1500;
      this._notifyValueChange();
      ConfigManager.updateChannelBindings({ [`ch${channelNum}`]: null });
    }

    // 刷新曲线缓存（曲线管理器更新后调用）
    refreshCurves() {
      this._buildCurveCache();
    }
  }

  global.ChannelKeyBinder = ChannelKeyBinder;
})(window);
