// Virtual joystick controller for touch/mobile usage.
(function (global) {
  class VirtualJoystickController {
    constructor({ elements = {}, onChange } = {}) {
      this.onChange = onChange;
      this.el = elements;
      this.active = false;
      this.center = { x: 0, y: 0 };
      this.radius = 1;
    }

    start() {
      if (!this.el.base || !this.el.handle) return;
      const base = this.el.base;
      const handle = this.el.handle;

      const updateCenter = () => {
        const rect = base.getBoundingClientRect();
        this.center = { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
        this.radius = Math.min(rect.width, rect.height) / 2;
      };

      const onMove = (clientX, clientY) => {
        if (!this.active) return;
        const dx = clientX - this.center.x;
        const dy = clientY - this.center.y;
        const distance = Math.min(Math.sqrt(dx * dx + dy * dy), this.radius);
        const normX = distance ? dx / this.radius : 0;
        const normY = distance ? dy / this.radius : 0;

        handle.style.transform = `translate(${normX * 50}%, ${normY * 50}%)`;
        // Y is inverted: up is negative screen delta
        this.onChange?.({ forward: -normY, turn: normX, active: true });
      };

      const stop = () => {
        this.active = false;
        handle.style.transform = "translate(-50%, -50%)";
        this.onChange?.({ forward: 0, turn: 0, active: false });
      };

      base.addEventListener("pointerdown", (e) => {
        updateCenter();
        this.active = true;
        base.setPointerCapture(e.pointerId);
        onMove(e.clientX, e.clientY);
      });

      base.addEventListener("pointermove", (e) => onMove(e.clientX, e.clientY));
      base.addEventListener("pointerup", stop);
      base.addEventListener("pointercancel", stop);
      window.addEventListener("resize", updateCenter);
      updateCenter();
    }
  }

  // Dual joystick controller: left for forward/back, right for left/right
  // Supports speed curves similar to keyboard controller based on hold duration
  class DualJoystickController {
    constructor({ elements = {}, onChange, curve = global.DEFAULT_SPEED_CURVE } = {}) {
      this.onChange = onChange;
      this.el = elements;
      this.active = false;
      this.forward = 0;
      this.turn = 0;
      this.curve = curve;

      // Track joystick hold states for speed curve
      this.joystickStates = {
        forward: { held: false, since: 0, direction: 0 },
        turn: { held: false, since: 0, direction: 0 }
      };

      this._tick = this._tick.bind(this);
    }

    start() {
      const leftBase = this.el.leftBase;
      const leftHandle = this.el.leftHandle;
      const rightBase = this.el.rightBase;
      const rightHandle = this.el.rightHandle;

      if (!leftBase || !leftHandle || !rightBase || !rightHandle) return;

      const updateCenter = (base, key) => {
        const rect = base.getBoundingClientRect();
        this[key] = {
          center: { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 },
          radius: Math.min(rect.width, rect.height) / 2
        };
      };

      const updateAllCenters = () => {
        updateCenter(leftBase, 'left');
        updateCenter(rightBase, 'right');
      };

      const onMove = (handle, clientX, clientY, key, axis) => {
        const centerData = this[key];
        if (!centerData) return;

        const dx = clientX - centerData.center.x;
        const dy = clientY - centerData.center.y;
        const distance = Math.min(Math.sqrt(dx * dx + dy * dy), centerData.radius);
        const normX = distance ? dx / centerData.radius : 0;
        const normY = distance ? dy / centerData.radius : 0;

        handle.style.transform = `translate(${normX * 50}%, ${normY * 50}%)`;

        // Update joystick state for speed curve
        const state = this.joystickStates[axis];
        if (distance > 0.01) {
          // Joystick is being held in some direction
          if (!state.held) {
            state.held = true;
            state.since = performance.now();
          }

          // Determine direction only (sign: -1 or 1), ignore magnitude
          // Speed curve will determine the actual value based on hold duration
          if (axis === 'forward') {
            state.direction = -normY < 0 ? -1 : 1; // Y is inverted
          } else if (axis === 'turn') {
            state.direction = normX < 0 ? -1 : 1;
          }
        } else {
          // Joystick is in center position
          state.held = false;
          state.direction = 0;
        }
      };

      const stopJoystick = (handle, axis) => {
        handle.style.transform = "translate(-50%, -50%)";

        // Reset joystick state
        const state = this.joystickStates[axis];
        state.held = false;
        state.direction = 0;
      };

      // Left joystick (forward/back)
      leftBase.addEventListener("pointerdown", (e) => {
        updateAllCenters();
        this.active = true;
        leftBase.setPointerCapture(e.pointerId);
        onMove(leftHandle, e.clientX, e.clientY, 'left', 'forward');
      });

      leftBase.addEventListener("pointermove", (e) => {
        onMove(leftHandle, e.clientX, e.clientY, 'left', 'forward');
      });

      leftBase.addEventListener("pointerup", () => stopJoystick(leftHandle, 'forward'));
      leftBase.addEventListener("pointercancel", () => stopJoystick(leftHandle, 'forward'));

      // Right joystick (left/right)
      rightBase.addEventListener("pointerdown", (e) => {
        updateAllCenters();
        this.active = true;
        rightBase.setPointerCapture(e.pointerId);
        onMove(rightHandle, e.clientX, e.clientY, 'right', 'turn');
      });

      rightBase.addEventListener("pointermove", (e) => {
        onMove(rightHandle, e.clientX, e.clientY, 'right', 'turn');
      });

      rightBase.addEventListener("pointerup", () => stopJoystick(rightHandle, 'turn'));
      rightBase.addEventListener("pointercancel", () => stopJoystick(rightHandle, 'turn'));

      window.addEventListener("resize", updateAllCenters);
      updateAllCenters();

      // Start tick loop for speed curve application
      requestAnimationFrame(this._tick);
    }

    // Apply speed curve based on hold duration
    _tick() {
      const now = performance.now();

      // Calculate forward axis with speed curve
      const forwardState = this.joystickStates.forward;
      if (forwardState.held && Math.abs(forwardState.direction) > 0.001) {
        const duration = now - forwardState.since;
        const curveValue = this.curve.sample(duration);
        // Apply curve value in the direction of joystick movement
        this.forward = forwardState.direction * curveValue;
      } else {
        this.forward = 0;
      }

      // Calculate turn axis with speed curve
      const turnState = this.joystickStates.turn;
      if (turnState.held && Math.abs(turnState.direction) > 0.001) {
        const duration = now - turnState.since;
        const curveValue = this.curve.sample(duration);
        // Apply curve value in the direction of joystick movement
        this.turn = turnState.direction * curveValue;
      } else {
        this.turn = 0;
      }

      const active = Math.abs(this.forward) > 0.001 || Math.abs(this.turn) > 0.001;

      this.onChange?.({
        forward: this.forward,
        turn: this.turn,
        active
      });

      requestAnimationFrame(this._tick);
    }

    // Update speed curve
    setCurve(curve) {
      this.curve = curve;
    }
  }

  global.VirtualJoystickController = VirtualJoystickController;
  global.DualJoystickController = DualJoystickController;
})(window);


