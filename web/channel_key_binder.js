// 通道按键绑定管理器
(function (global) {
  class ChannelKeyBinder {
    constructor() {
      this.bindings = {};
      this.keyStates = {};
      this.channelValues = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
      this.activeKey = null;
      this.activeChannel = null;
      this.onValueChange = null;
      this.keydownHandler = null;
      this.keyupHandler = null;
    }

    // 初始化绑定配置
    init(bindings) {
      this.bindings = bindings || {};
      console.log('通道按键绑定已初始化:', this.bindings);
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
      if (this.keydownHandler || this.keyupHandler) {
        return; // 已经启动
      }

      this.keydownHandler = (e) => this._handleKeydown(e);
      this.keyupHandler = (e) => this._handleKeyup(e);

      window.addEventListener('keydown', this.keydownHandler);
      window.addEventListener('keyup', this.keyupHandler);
    }

    // 停止键盘监听
    stop() {
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

      // 查找是否有绑定到这个键的通道
      for (let i = 1; i <= 16; i++) {
        const binding = this.bindings[`ch${i}`];
        if (!binding) continue;

        if (binding.type === 'single') {
          // 一档模式：按下置为 value，不按为 0
          if (binding.key === code) {
            e.preventDefault();
            this.channelValues[i - 1] = binding.value;
            this._notifyValueChange();
            return;
          }
        } else if (binding.type === 'continuous') {
          // 连续模式：指定范围
          if (binding.negativeKey === code) {
            e.preventDefault();
            this.keyStates[code] = true;
            this.activeChannel = i;
            this._updateContinuousValue(i, -1);
            return;
          }
          if (binding.positiveKey === code) {
            e.preventDefault();
            this.keyStates[code] = true;
            this.activeChannel = i;
            this._updateContinuousValue(i, 1);
            return;
          }
        }
      }
    }

    // 处理键盘抬起
    _handleKeyup(e) {
      const code = e.code;

      // 检查是否是一档模式的键
      for (let i = 1; i <= 16; i++) {
        const binding = this.bindings[`ch${i}`];
        if (!binding) continue;

        if (binding.type === 'single') {
          // 一档模式：不按为 0
          if (binding.key === code) {
            e.preventDefault();
            this.channelValues[i - 1] = 0;
            this._notifyValueChange();
            return;
          }
        }
      }

      // 检查是否是连续模式的键
      for (let i = 1; i <= 16; i++) {
        const binding = this.bindings[`ch${i}`];
        if (!binding) continue;

        if (binding.type === 'continuous') {
          if (binding.negativeKey === code || binding.positiveKey === code) {
            this.keyStates[code] = false;
            // 检查该通道是否还有其他键被按下
            const hasNegative = this.keyStates[binding.negativeKey];
            const hasPositive = this.keyStates[binding.positiveKey];

            if (!hasNegative && !hasPositive) {
              // 没有按键，重置为起始点
              this.channelValues[i - 1] = binding.startValue || 0;
              this._notifyValueChange();
            } else if (hasNegative) {
              this._updateContinuousValue(i, -1);
            } else if (hasPositive) {
              this._updateContinuousValue(i, 1);
            }
            return;
          }
        }
      }
    }

    // 更新连续模式值
    _updateContinuousValue(channelNum, direction) {
      const binding = this.bindings[`ch${channelNum}`];
      if (!binding) return;

      const minValue = binding.minValue !== undefined ? binding.minValue : -1.0;
      const maxValue = binding.maxValue !== undefined ? binding.maxValue : 1.0;

      if (direction === -1) {
        this.channelValues[channelNum - 1] = minValue;
      } else {
        this.channelValues[channelNum - 1] = maxValue;
      }

      this._notifyValueChange();
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
      if (!newBinding) return null; // 清除绑定不检查冲突
      
      const conflicts = [];
      const newKeys = [];
      
      // 收集新绑定的所有按键
      if (newBinding.type === 'single') {
        if (newBinding.key) newKeys.push(newBinding.key);
      } else if (newBinding.type === 'continuous') {
        if (newBinding.negativeKey) newKeys.push(newBinding.negativeKey);
        if (newBinding.positiveKey) newKeys.push(newBinding.positiveKey);
        
        // 检查内部冲突：连续模式的负值和正值不能相同
        if (newBinding.negativeKey && newBinding.positiveKey && newBinding.negativeKey === newBinding.positiveKey) {
          conflicts.push({
            channel: channelNum,
            key: newBinding.negativeKey,
            type: 'internal',
            message: `连续模式的负值和正值不能设置为同一按键`
          });
        }
      }
      
      // 如果有内部冲突，直接返回
      if (conflicts.length > 0) {
        return conflicts[0];
      }
      
      // 检查所有新按键是否与现有按键冲突
      for (const newKey of newKeys) {
        // 检查所有通道（包括自身通道的其他按键）
        for (let i = 1; i <= 16; i++) {
          // 如果是自身通道，需要检查是否与同通道的其他按键冲突
          if (i === channelNum) {
            const existingBinding = this.bindings[`ch${i}`];
            if (!existingBinding) continue;
            
            // 同通道内冲突检测
            if (newBinding.type === 'continuous' && existingBinding.type === 'continuous') {
              // 连续模式内部：负值和正值不能相同（前面已检查）
              // 这里不需要额外检查，因为前面已经检查过
            } else {
              // 检查新按键是否与同通道的其他模式按键冲突
              const existingKeys = [];
              if (existingBinding.type === 'single' && existingBinding.key) {
                existingKeys.push(existingBinding.key);
              } else if (existingBinding.type === 'continuous') {
                if (existingBinding.negativeKey) existingKeys.push(existingBinding.negativeKey);
                if (existingBinding.positiveKey) existingKeys.push(existingBinding.positiveKey);
              }
              
              // 如果新按键已经在同通道的其他按键中，说明冲突
              if (existingKeys.includes(newKey)) {
                conflicts.push({
                  channel: i,
                  key: newKey,
                  type: 'same-channel',
                  message: `按键 ${this.formatKeyName(newKey)} 在当前通道的其他模式中已使用`
                });
                break; // 找到一个冲突就够了
              }
            }
            continue; // 自身通道的同模式冲突已在上面处理
          }
          
          // 检查其他通道的冲突
          const existingBinding = this.bindings[`ch${i}`];
          if (!existingBinding) continue;
          
          // 检查一档模式冲突
          if (existingBinding.type === 'single' && existingBinding.key === newKey) {
            conflicts.push({
              channel: i,
              key: newKey,
              type: 'single',
              message: `按键 ${this.formatKeyName(newKey)} 已被通道${i}的一档模式占用`
            });
            break; // 找到一个冲突就够了
          }
          
          // 检查连续模式冲突
          if (existingBinding.type === 'continuous') {
            if (existingBinding.negativeKey === newKey) {
              conflicts.push({
                channel: i,
                key: newKey,
                type: 'continuous-negative',
                message: `按键 ${this.formatKeyName(newKey)} 已被通道${i}的连续模式负值占用`
              });
              break; // 找到一个冲突就够了
            }
            if (existingBinding.positiveKey === newKey) {
              conflicts.push({
                channel: i,
                key: newKey,
                type: 'continuous-positive',
                message: `按键 ${this.formatKeyName(newKey)} 已被通道${i}的连续模式正值占用`
              });
              break; // 找到一个冲突就够了
            }
          }
        }
        
        // 如果找到冲突就跳出外层循环
        if (conflicts.length > 0) {
          break;
        }
      }
      
      return conflicts.length > 0 ? conflicts[0] : null; // 返回第一个冲突
    }

    // 格式化按键名
    formatKeyName(code) {
      if (!code) return '';
      return code.replace('Key', '').replace('Digit', '');
    }

    // 设置通道绑定
    setBinding(channelNum, binding) {
      // 检查按键冲突
      const conflict = this.checkKeyConflict(channelNum, binding);
      if (conflict) {
        console.warn(`按键冲突: ${conflict.message}`);
        alert(`按键冲突: ${conflict.message}`);
        return false;
      }
      
      this.bindings[`ch${channelNum}`] = binding;
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
      this.channelValues[channelNum - 1] = 0;
      this._notifyValueChange();
      // 保存到配置
      ConfigManager.updateChannelBindings({ [`ch${channelNum}`]: null });
    }
  }

  global.ChannelKeyBinder = ChannelKeyBinder;
})(window);
