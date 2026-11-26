package recomp.vr1.android;

import android.content.Context;
import android.content.pm.PackageManager;
import android.hardware.input.InputManager;
import android.net.wifi.WifiManager;
import android.os.Bundle;
import android.util.Log;
import android.view.InputDevice;
import android.view.View;
import android.view.ViewGroup;

import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity implements InputManager.InputDeviceListener {
    private InputManager inputManager;
    private TouchControls touchControls;
    private boolean resumed, menuOpen;
    private boolean lanDiscovery;
    private WifiManager.MulticastLock multicastLock;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (mBrokenLibraries || mLayout == null) return;
        inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
        touchControls = new TouchControls(this);
        mLayout.addView(touchControls, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        inputManager.registerInputDeviceListener(this, null);
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        updateTouchControls();
        updateLanDiscovery();
    }

    @Override
    protected void onPause() {
        resumed = false;
        updateLanDiscovery();
        if (touchControls != null) touchControls.setActive(false);
        super.onPause();
    }

    private void updateTouchControls() {
        if (touchControls == null) return;
        boolean controller = false;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.isVirtual()) continue;
            int sources = device.getSources();
            if ((sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                    || (sources & InputDevice.SOURCE_CLASS_JOYSTICK) != 0) {
                controller = true;
                break;
            }
        }
        boolean enabled = resumed && !controller
                && getPackageManager().hasSystemFeature(PackageManager.FEATURE_TOUCHSCREEN);
        touchControls.setActive(enabled);
        touchControls.setVisibility(enabled && !menuOpen ? View.VISIBLE : View.GONE);
    }

    public void setTouchUiState(boolean open, boolean pcMode) {
        runOnUiThread(() -> {
            menuOpen = open;
            if (touchControls != null) touchControls.setUiState(open, pcMode);
            updateTouchControls();
        });
    }

    public void setLanDiscovery(boolean enabled) {
        runOnUiThread(() -> {
            lanDiscovery = enabled;
            updateLanDiscovery();
        });
    }

    private void updateLanDiscovery() {
        if (resumed && lanDiscovery) {
            if (multicastLock == null) {
                WifiManager wifi = (WifiManager) getApplicationContext().getSystemService(Context.WIFI_SERVICE);
                if (wifi == null) {
                    Log.e("Model1LAN", "Wi-Fi service unavailable; LAN discovery may not work");
                    return;
                }
                multicastLock = wifi.createMulticastLock("Model1LAN");
                multicastLock.setReferenceCounted(false);
            }
            if (!multicastLock.isHeld()) multicastLock.acquire();
        } else if (multicastLock != null && multicastLock.isHeld()) {
            multicastLock.release();
        }
    }

    @Override public void onInputDeviceAdded(int id) { updateTouchControls(); }
    @Override public void onInputDeviceRemoved(int id) { updateTouchControls(); }
    @Override public void onInputDeviceChanged(int id) { updateTouchControls(); }

    @Override
    protected void onDestroy() {
        lanDiscovery = false;
        updateLanDiscovery();
        if (inputManager != null) inputManager.unregisterInputDeviceListener(this);
        if (touchControls != null) touchControls.setActive(false);
        super.onDestroy();
        // SDL does not normally allow its native main to be started twice in one process.
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
