// Simple, extensible speed curves for different controllers.
(function (global) {
  class SpeedCurve {
    constructor(points) {
      this.points = (points || []).sort((a, b) => a.t - b.t);
    }

    sample(ms) {
      if (this.points.length === 0) return 0;
      if (ms <= this.points[0].t) return this.points[0].v;
      for (let i = 1; i < this.points.length; i++) {
        const prev = this.points[i - 1];
        const curr = this.points[i];
        if (ms <= curr.t) {
          const ratio = (ms - prev.t) / (curr.t - prev.t);
          return prev.v + ratio * (curr.v - prev.v);
        }
      }
      return this.points[this.points.length - 1].v;
    }

    // Get curve duration
    getDuration() {
      if (this.points.length === 0) return 0;
      return this.points[this.points.length - 1].t;
    }

    // Get max speed
    getMaxSpeed() {
      if (this.points.length === 0) return 0;
      return Math.max(...this.points.map(p => p.v));
    }

    // Clone curve
    clone() {
      return new SpeedCurve(JSON.parse(JSON.stringify(this.points)));
    }

    // Validate points
    static validatePoints(points) {
      if (!Array.isArray(points) || points.length < 2) {
        return { valid: false, error: '至少需要2个控制点' };
      }

      // Check first point is at t=0, v=0
      if (points[0].t !== 0 || points[0].v !== 0) {
        return { valid: false, error: '第一个点必须是 t=0, v=0' };
      }

      // Check last point v=1
      if (points[points.length - 1].v !== 1) {
        return { valid: false, error: '最后一个点的速度必须为 1.0' };
      }

      // Check times are sorted
      for (let i = 1; i < points.length; i++) {
        if (points[i].t <= points[i - 1].t) {
          return { valid: false, error: '时间必须递增' };
        }
      }

      // Check v values are in [0, 1]
      for (const p of points) {
        if (p.v < 0 || p.v > 1) {
          return { valid: false, error: '速度值必须在 0.0-1.0 之间' };
        }
      }

      return { valid: true };
    }
  }

  const DEFAULT_SPEED_CURVE = new SpeedCurve([
    { t: 0, v: 0.0 },
    { t: 150, v: 0.3 },
    { t: 400, v: 0.6 },
    { t: 800, v: 0.85 },
    { t: 1200, v: 1.0 },
  ]);

  global.SpeedCurve = SpeedCurve;
  global.DEFAULT_SPEED_CURVE = DEFAULT_SPEED_CURVE;
})(window);

