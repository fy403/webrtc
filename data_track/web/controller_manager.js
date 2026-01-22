// Aggregates multiple controllers and picks the active source.
(function (global) {
  class ControllerManager {
    constructor(onUpdate) {
      this.onUpdate = onUpdate;
      this.controllers = new Map();
      this.lastSent = { forward: 0, turn: 0 };
      this.onControllerStatusChange = null;
    }

    register(name, controller, priority = 0) {
      this.controllers.set(name, { controller, priority, state: { forward: 0, turn: 0, active: false } });
      controller.onChange = (state) => this._handle(name, state);
      if (controller.start) controller.start();
      this._notifyStatusChange();
    }

    _handle(name, state) {
      const item = this.controllers.get(name);
      if (!item) return;
      item.state = state;

      let chosen = { forward: 0, turn: 0, active: false, priority: -Infinity, name: "" };
      this.controllers.forEach((c, key) => {
        if (c.state.active) {
          if (!chosen.active || c.priority > chosen.priority) {
            chosen = { ...c.state, priority: c.priority, name: key };
          }
        }
      });

      // If none active, send neutral once
      if (!chosen.active) {
        chosen = { forward: 0, turn: 0, active: false };
      }

      if (
        Math.abs(chosen.forward - this.lastSent.forward) > 0.005 ||
        Math.abs(chosen.turn - this.lastSent.turn) > 0.005 ||
        chosen.active !== this.lastSent.active
      ) {
        this.lastSent = chosen;
        this.onUpdate?.(chosen);
      }

      this._notifyStatusChange();
    }

    _notifyStatusChange() {
      if (this.onControllerStatusChange) {
        const statuses = {};
        this.controllers.forEach((item, name) => {
          statuses[name] = item.state.active;
        });
        this.onControllerStatusChange(statuses);
      }
    }

    setControllerStatusCallback(callback) {
      this.onControllerStatusChange = callback;
    }
  }

  global.ControllerManager = ControllerManager;
})(window);


