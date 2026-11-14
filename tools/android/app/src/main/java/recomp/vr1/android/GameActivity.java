package recomp.vr1.android;

import org.libsdl.app.SDLActivity;

public class GameActivity extends SDLActivity {
    @Override
    protected void onDestroy() {
        super.onDestroy();
        // SDL does not normally allow its native main to be started twice in one process.
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
