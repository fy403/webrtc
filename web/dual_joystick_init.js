// Initialize dual joystick controller for mobile
window.addEventListener('load', () => {
    // Wait for controllerManager to be available
    setTimeout(() => {
        if (window.controllerManager) {
            const dualJoystick = new DualJoystickController({
                elements: {
                    leftBase: document.getElementById('leftJoystickBase'),
                    leftHandle: document.getElementById('leftJoystickHandle'),
                    rightBase: document.getElementById('rightJoystickBase'),
                    rightHandle: document.getElementById('rightJoystickHandle'),
                },
            });

            // Register dual joystick with higher priority than single joystick
            window.controllerManager.register('dualJoystick', dualJoystick, 6);

            // Unregister single joystick if it was registered
            if (window.controllerManager.controllers.has('joystick')) {
                window.controllerManager.controllers.delete('joystick');
                console.log('Unregistered single joystick in favor of dual joystick');
            }

            console.log('Dual joystick controller initialized and registered');
        } else {
            console.error('ControllerManager not available');
        }
    }, 100);
});
