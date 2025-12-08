// Xbox / standard Gamepad controller using the browser Gamepad API.
// Maps left stick Y -> forward/backward, left stick X -> turn.
(function (global) {
  const DEADZONE = 0.08;
  const POLL_MS = 16; // ~60fps

  function applyDeadzone(v) {
    return Math.abs(v) < DEADZONE ? 0 : v;
  }

  class XboxController {
    constructor({ onChange } = {}) {
      this.onChange = onChange;
      this.active = false;
      this.last = { forward: 0, turn: 0 };
      this.connectedIndex = null;
      this._raf = null;
      this._timer = null;
    }

    start() {
      window.addEventListener('gamepadconnected', this._onConnect);
      window.addEventListener('gamepaddisconnected', this._onDisconnect);
      // Start polling even before explicit connect to catch already-connected pads.
      this._schedulePoll();
    }

    stop() {
      window.removeEventListener('gamepadconnected', this._onConnect);
      window.removeEventListener('gamepaddisconnected', this._onDisconnect);
      if (this._raf) cancelAnimationFrame(this._raf);
      if (this._timer) clearTimeout(this._timer);
    }

    _onConnect = (e) => {
      this.connectedIndex = e.gamepad.index;
    };

    _onDisconnect = (e) => {
      if (this.connectedIndex === e.gamepad.index) {
        this.connectedIndex = null;
        this._emit(0, 0, false);
      }
    };

    _poll = () => {
      const pads = navigator.getGamepads ? navigator.getGamepads() : [];
      const pad = this.connectedIndex !== null ? pads[this.connectedIndex] : pads.find((p) => p && p.connected);
      if (pad) {
        this.connectedIndex = pad.index;
        const rx = applyDeadzone(pad.axes[2] || 0); // right stick X for turn
        const lt = pad.buttons[6] ? pad.buttons[6].value || 0 : 0; // LT
        const rt = pad.buttons[7] ? pad.buttons[7].value || 0 : 0; // RT

        const forward = rt - lt; // RT forward, LT backward
        const turn = rx;
        const active = Math.abs(forward) > 0.02 || Math.abs(turn) > 0.02;
        this._emit(forward, turn, active);
      } else {
        this._emit(0, 0, false);
      }
      this._schedulePoll();
    };

    _schedulePoll() {
      this._timer = setTimeout(() => {
        this._raf = requestAnimationFrame(this._poll);
      }, POLL_MS);
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
  }

  global.XboxController = XboxController;
})(window);

