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

