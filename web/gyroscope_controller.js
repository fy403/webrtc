// Gyroscope controller for mobile devices using DeviceOrientation API.
// Maps device rotation (beta for pitch, gamma for roll) to forward/turn control.
(function (global) {
  class GyroscopeController {
    constructor({ onChange, sensitivity = 0.6, deadzone = 5 } = {}) {
      this.onChange = onChange;
      this.sensitivity = sensitivity;
      this.deadzone = deadzone;
      this.active = false;
      this.last = { forward: 0, turn: 0 };
      this.initialBeta = 0;
      this.initialGamma = 0;
      this.hasPermission = false;
      this.handleOrientation = null;
      this.calibrated = false;  // 是否已校准
    }

    async requestPermission() {
      if (typeof DeviceOrientationEvent === 'undefined') {
        console.warn('DeviceOrientationEvent not supported');
        return false;
      }

      if (typeof DeviceOrientationEvent.requestPermission === 'function') {
        try {
          const permissionState = await DeviceOrientationEvent.requestPermission();
          console.log('Permission state:', permissionState);
          if (permissionState === 'granted') {
            this.hasPermission = true;
            return true;
          }
          console.log('Permission denied:', permissionState);
          return false;
        } catch (error) {
          console.error('Error requesting device orientation permission:', error);
          return false;
        }
      } else {
        // iOS 13+ 需要 requestPermission，其他版本不需要
        // 检查是否为iOS设备
        const isIOS = /iPad|iPhone|iPod/.test(navigator.userAgent) && !window.MSStream;
        if (isIOS) {
          // iOS 13+，检查是否支持 requestPermission
          console.log('iOS detected but requestPermission not available');
          // 尝试直接使用，某些设备可能不需要权限
          this.hasPermission = true;
          return true;
        } else {
          // 非iOS设备，不需要权限
          console.log('Non-iOS device, permission not required');
          this.hasPermission = true;
          return true;
        }
      }
    }

    start() {
      if (!this.hasPermission) {
        console.warn('Gyroscope permission not granted, hasPermission:', this.hasPermission);
        // 不要返回，允许在已授权的情况下启动
      }

      console.log('Starting gyroscope controller...');
      // 重置校准状态，准备重新校准
      this.calibrated = false;
      this.initialBeta = 0;
      this.initialGamma = 0;

      // 移除旧的事件监听器（如果有）
      if (this.handleOrientation) {
        window.removeEventListener('deviceorientation', this.handleOrientation);
      }

      this.handleOrientation = (event) => this._handleOrientation(event);
      window.addEventListener('deviceorientation', this.handleOrientation);
      console.log('Device orientation listener attached');
    }

    stop() {
      if (this.handleOrientation) {
        window.removeEventListener('deviceorientation', this.handleOrientation);
        this.handleOrientation = null;
      }
      this.active = false;
      this._emit(0, 0, false);
    }

    // 重新校准到当前位置
    recalibrate(beta, gamma) {
      this.initialBeta = beta;
      this.initialGamma = gamma;
      this.calibrated = true;
      console.log('Gyroscope recalibrated - Beta:', beta, 'Gamma:', gamma);
    }

    _handleOrientation(event) {
      if (event.beta === null || event.gamma === null) return;

      // Initialize reference values on first valid event (auto-calibrate)
      if (!this.calibrated) {
        this.initialBeta = event.beta;
        this.initialGamma = event.gamma;
        this.calibrated = true;
        console.log('Gyroscope auto-calibrated - Beta:', event.beta, 'Gamma:', event.gamma);
      }

      // Calculate offset from initial orientation
      const betaOffset = event.beta - this.initialBeta;
      const gammaOffset = event.gamma - this.initialGamma;

      // Beta: 向前倾斜为负值，向后倾斜为正值
      // 我们需要反转：向前倾斜应该加速（forward > 0）
      // 所以 betaOffset < 0 时应该产生正值
      const forwardValue = -betaOffset;

      // Apply deadzone - 使用更大的死区来减少抖动
      const forwardRaw = this._applyDeadzone(forwardValue);
      const turnRaw = this._applyDeadzone(gammaOffset);

      // Apply sensitivity
      const forward = forwardRaw * this.sensitivity;
      const turn = turnRaw * this.sensitivity;

      // Normalize to [-1, 1] range - 使用更大的最大角度来进一步降低灵敏度
      const normForward = this._normalize(forward, 60);
      const normTurn = this._normalize(turn, 60);

      // Check if active (beyond threshold) - 增加阈值
      const active = Math.abs(normForward) > 0.08 || Math.abs(normTurn) > 0.08;

      if (active) {
        console.log('Gyroscope - Forward:', normForward.toFixed(3), 'Turn:', normTurn.toFixed(3), 'Beta:', event.beta.toFixed(1), 'Gamma:', event.gamma.toFixed(1));
      }

      this._emit(normForward, normTurn, active);
    }

    _applyDeadzone(value) {
      return Math.abs(value) < this.deadzone ? 0 : value;
    }

    _normalize(value, maxAngle) {
      return Math.max(-1, Math.min(1, value / maxAngle));
    }

    _emit(forward, turn, active) {
      if (
        Math.abs(forward - this.last.forward) > 0.01 ||
        Math.abs(turn - this.last.turn) > 0.01 ||
        active !== this.last.active
      ) {
        this.last = { forward, turn, active };
        this.onChange?.({ forward, turn, active });
      }
    }

    isSupported() {
      return 'DeviceOrientationEvent' in window && 
             typeof DeviceOrientationEvent !== 'undefined';
    }

    requiresPermission() {
      return typeof DeviceOrientationEvent !== 'undefined' &&
             typeof DeviceOrientationEvent.requestPermission === 'function';
    }
  }

  global.GyroscopeController = GyroscopeController;
})(window);
