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
  class DualJoystickController {
    constructor({ elements = {}, onChange } = {}) {
      this.onChange = onChange;
      this.el = elements;
      this.active = false;
      this.forward = 0;
      this.turn = 0;
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

      const onMove = (base, handle, clientX, clientY, key, axis) => {
        const centerData = this[key];
        if (!centerData) return;

        const dx = clientX - centerData.center.x;
        const dy = clientY - centerData.center.y;
        const distance = Math.min(Math.sqrt(dx * dx + dy * dy), centerData.radius);
        const normX = distance ? dx / centerData.radius : 0;
        const normY = distance ? dy / centerData.radius : 0;

        handle.style.transform = `translate(${normX * 50}%, ${normY * 50}%)`;

        // Left joystick: forward/back (Y axis)
        // Right joystick: left/right (X axis)
        if (axis === 'forward') {
          this.forward = -normY;
        } else if (axis === 'turn') {
          this.turn = normX;
        }

        this.onChange?.({
          forward: this.forward,
          turn: this.turn,
          active: true
        });
      };

      const stopJoystick = (handle, axis) => {
        handle.style.transform = "translate(-50%, -50%)";
        if (axis === 'forward') {
          this.forward = 0;
        } else if (axis === 'turn') {
          this.turn = 0;
        }

        this.onChange?.({
          forward: this.forward,
          turn: this.turn,
          active: this.forward !== 0 || this.turn !== 0
        });
      };

      // Left joystick (forward/back)
      leftBase.addEventListener("pointerdown", (e) => {
        updateAllCenters();
        this.active = true;
        leftBase.setPointerCapture(e.pointerId);
        onMove(leftBase, leftHandle, e.clientX, e.clientY, 'left', 'forward');
      });

      leftBase.addEventListener("pointermove", (e) => {
        onMove(leftBase, leftHandle, e.clientX, e.clientY, 'left', 'forward');
      });

      leftBase.addEventListener("pointerup", () => stopJoystick(leftHandle, 'forward'));
      leftBase.addEventListener("pointercancel", () => stopJoystick(leftHandle, 'forward'));

      // Right joystick (left/right)
      rightBase.addEventListener("pointerdown", (e) => {
        updateAllCenters();
        this.active = true;
        rightBase.setPointerCapture(e.pointerId);
        onMove(rightBase, rightHandle, e.clientX, e.clientY, 'right', 'turn');
      });

      rightBase.addEventListener("pointermove", (e) => {
        onMove(rightBase, rightHandle, e.clientX, e.clientY, 'right', 'turn');
      });

      rightBase.addEventListener("pointerup", () => stopJoystick(rightHandle, 'turn'));
      rightBase.addEventListener("pointercancel", () => stopJoystick(rightHandle, 'turn'));

      window.addEventListener("resize", updateAllCenters);
      updateAllCenters();
    }
  }

  global.VirtualJoystickController = VirtualJoystickController;
  global.DualJoystickController = DualJoystickController;
})(window);


