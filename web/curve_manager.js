// Speed Curve Manager - Manages different acceleration curves
(function (global) {
  class SpeedCurveManager {
    constructor() {
      // Preset curves with different acceleration profiles
      this.presets = {
        linear: {
          name: '线性加速度',
          description: '均匀加速，适合模拟真实驾驶',
          points: this._generateLinearCurve()
        },
        exponential: {
          name: '指数/渐进加速度',
          description: '前期慢后期快，快速切换速度',
          points: this._generateExponentialCurve()
        },
      };

      // Custom curves saved by user
      this.customCurves = this._loadCustomCurves();

      // Current active curve
      this.currentCurveId = localStorage.getItem('speedCurveId') || 'linear';
      console.log('[SpeedCurveManager] Current curve ID:', this.currentCurveId);

      // Callbacks
      this.onChange = null;
    }

    // Generate linear acceleration curve
    _generateLinearCurve() {
      return [
        { t: 0, v: 0.0 },
        { t: 200, v: 0.25 },
        { t: 400, v: 0.5 },
        { t: 600, v: 0.75 },
        { t: 800, v: 1.0 }
      ];
    }

    // Generate exponential acceleration curve
    _generateExponentialCurve() {
      return [
        { t: 0, v: 0.0 },
        { t: 100, v: 0.05 },
        { t: 200, v: 0.15 },
        { t: 300, v: 0.3 },
        { t: 400, v: 0.5 },
        { t: 500, v: 0.75 },
        { t: 600, v: 0.9 },
        { t: 700, v: 1.0 }
      ];
    }

    // Load custom curves from localStorage
    _loadCustomCurves() {
      try {
        const saved = localStorage.getItem('customSpeedCurves');
        const curves = saved ? JSON.parse(saved) : {};
        console.log('[SpeedCurveManager] Loaded', Object.keys(curves).length, 'custom curves');
        return curves;
      } catch (e) {
        console.error('[SpeedCurveManager] Failed to load custom curves:', e);
        return {};
      }
    }

    // Reload custom curves from localStorage (for cross-tab sync)
    reloadCustomCurves() {
      this.customCurves = this._loadCustomCurves();
      console.log('[SpeedCurveManager] Reloaded', Object.keys(this.customCurves).length, 'custom curves');
    }

    // Save custom curves to localStorage
    _saveCustomCurves() {
      localStorage.setItem('customSpeedCurves', JSON.stringify(this.customCurves));
      console.log('[SpeedCurveManager] Saved', Object.keys(this.customCurves).length, 'custom curves');
    }

    // Get all available curves
    getAllCurves() {
      return {
        ...this.presets,
        ...this.customCurves
      };
    }

    // Get curve by ID
    getCurve(id) {
      const allCurves = this.getAllCurves();
      return allCurves[id];
    }

    // Get current curve
    getCurrentCurve() {
      return this.getCurve(this.currentCurveId);
    }

    // Set current curve
    setCurve(id) {
      if (!this.getCurve(id)) {
        throw new Error(`Curve "${id}" not found`);
      }
      this.currentCurveId = id;
      localStorage.setItem('speedCurveId', id);
      const curve = this.getCurrentCurve();
      console.log('[SpeedCurveManager] Curve set to:', id, '-', curve.name);
      this.onChange?.(this.getCurrentCurve());
    }

    // Create a custom curve
    createCustomCurve(name, points, description = '') {
      const id = 'custom_' + Date.now();
      this.customCurves[id] = {
        name,
        description,
        points,
        isCustom: true
      };
      this._saveCustomCurves();
      return id;
    }

    // Update a custom curve
    updateCustomCurve(id, updates) {
      if (!this.customCurves[id]) {
        throw new Error(`Custom curve "${id}" not found`);
      }
      this.customCurves[id] = { ...this.customCurves[id], ...updates };
      this._saveCustomCurves();
      
      // If updating current curve, notify change
      if (this.currentCurveId === id) {
        this.onChange?.(this.getCurrentCurve());
      }
    }

    // Delete a custom curve
    deleteCustomCurve(id) {
      if (!this.customCurves[id]) {
        throw new Error(`Custom curve "${id}" not found`);
      }
      delete this.customCurves[id];
      this._saveCustomCurves();
      
      // If deleted curve was current, switch to default
      if (this.currentCurveId === id) {
        this.setCurve('linear');
      }
    }

    // Get SpeedCurve object for current curve
    getCurrentSpeedCurve() {
      const curve = this.getCurrentCurve();
      if (!curve) {
        return new global.SpeedCurve(this._generateLinearCurve());
      }
      return new global.SpeedCurve(curve.points);
    }

    // Set change callback
    setOnChange(callback) {
      this.onChange = callback;
    }
  }

  // Create singleton instance
  const speedCurveManager = new SpeedCurveManager();

  global.SpeedCurveManager = SpeedCurveManager;
  global.speedCurveManager = speedCurveManager;
})(window);
