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

  global.VirtualJoystickController = VirtualJoystickController;
})(window);

