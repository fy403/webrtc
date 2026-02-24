// Initialize dual joystick controller for mobile
window.addEventListener('load', () => {
    // Wait for controllerManager to be available
    setTimeout(() => {
        if (window.controllerManager && window.speedCurveManager) {
            // Get current speed curve from curve manager (same as keyboard controller)
            const currentSpeedCurve = window.speedCurveManager.getCurrentSpeedCurve() || window.DEFAULT_SPEED_CURVE;
            const currentCurve = window.speedCurveManager.getCurrentCurve();
            const curveName = currentCurve ? currentCurve.name : 'Default';
            console.log('Loading speed curve for dual joystick:', window.speedCurveManager.currentCurveId, curveName);

            const dualJoystick = new DualJoystickController({
                elements: {
                    leftBase: document.getElementById('leftJoystickBase'),
                    leftHandle: document.getElementById('leftJoystickHandle'),
                    rightBase: document.getElementById('rightJoystickBase'),
                    rightHandle: document.getElementById('rightJoystickHandle'),
                },
                curve: currentSpeedCurve,
            });

            // Register dual joystick with higher priority than single joystick
            window.controllerManager.register('dualJoystick', dualJoystick, 6);

            // Unregister single joystick if it was registered
            if (window.controllerManager.controllers.has('joystick')) {
                window.controllerManager.controllers.delete('joystick');
                console.log('Unregistered single joystick in favor of dual joystick');
            }

            // Set up curve change callback - update dual joystick curve when it changes
            window.speedCurveManager.setOnChange((curve) => {
                if (curve) {
                    dualJoystick.setCurve(new window.SpeedCurve(curve.points));
                    console.log('Dual joystick curve updated to:', curve.name);
                }
            });

            console.log('Dual joystick controller initialized and registered with speed curve support');
        } else {
            console.error('ControllerManager or SpeedCurveManager not available');
        }
    }, 100);
});
