// Keyboard controller that produces normalized SBUS axes with ramp-up curves.
(function (global) {
  class KeyboardController {
    constructor({ curve = global.DEFAULT_SPEED_CURVE, onChange, onVisualChange } = {}) {
      this.curve = curve;
      this.onChange = onChange;
      this.onVisualChange = onVisualChange;
      this.state = {
        W: { pressed: false, since: 0 },
        S: { pressed: false, since: 0 },
        A: { pressed: false, since: 0 },
        D: { pressed: false, since: 0 },
      };
      this.active = false;
      this.lastSent = { forward: 0, turn: 0 };
      this._tick = this._tick.bind(this);
    }

    start() {
      this.keydownHandler = (e) => this._handleKey(e, true);
      this.keyupHandler = (e) => this._handleKey(e, false);
      window.addEventListener("keydown", this.keydownHandler);
      window.addEventListener("keyup", this.keyupHandler);
      requestAnimationFrame(this._tick);
    }

    stop() {
      window.removeEventListener("keydown", this.keydownHandler);
      window.removeEventListener("keyup", this.keyupHandler);
    }

    _handleKey(ev, isDown) {
      const key = ev.code?.replace("Key", "");
      if (!["W", "A", "S", "D"].includes(key)) return;
      const record = this.state[key];
      if (record.pressed === isDown) return;
      record.pressed = isDown;
      record.since = performance.now();
      this.onVisualChange?.(this.state);
    }

    _axisValue(positiveKey, negativeKey, now) {
      const pos = this.state[positiveKey];
      const neg = this.state[negativeKey];
      const posVal = pos.pressed ? this.curve.sample(now - pos.since) : 0;
      const negVal = neg.pressed ? this.curve.sample(now - neg.since) : 0;
      return posVal - negVal;
    }

    _tick() {
      const now = performance.now();
      const forward = this._axisValue("W", "S", now);
      const turn = this._axisValue("D", "A", now);
      const active = Math.abs(forward) > 0.001 || Math.abs(turn) > 0.001;

      if (
        active !== this.active ||
        Math.abs(forward - this.lastSent.forward) > 0.005 ||
        Math.abs(turn - this.lastSent.turn) > 0.005
      ) {
        this.active = active;
        this.lastSent = { forward, turn };
        this.onChange?.({ forward, turn, active });
      }

      requestAnimationFrame(this._tick);
    }
  }

  global.KeyboardController = KeyboardController;
})(window);

