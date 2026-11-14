package recomp.vr1.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.content.res.AssetManager;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;

public class LauncherActivity extends Activity {
    private static final int PICK_GAME = 1;
    private static final int PICK_IO = 2;
    private static final int EXPORT_LOG = 3;
    private TextView status;
    private Button gameButton, ioButton, playButton, exportButton;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        int padding = (int) (24 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        TextView title = new TextView(this);
        title.setText("Virtua Racing Recomp — Android prototype");
        title.setTextSize(22);
        layout.addView(title);
        status = new TextView(this);
        layout.addView(status);
        gameButton = button(layout, "Import vr.zip", () -> pick(PICK_GAME));
        ioButton = button(layout, "Import model1io.zip", () -> pick(PICK_IO));
        playButton = button(layout, "Play", this::play);
        playButton.setFocusableInTouchMode(true);
        button(layout, "Licences", () -> showLicenses("LICENSE", "LICENSES.txt"));
        button(layout, "Android toolchain licences", () -> showLicenses("NDK-NOTICE.txt", "NDK-NOTICE.toolchain.txt"));
        exportButton = button(layout, "Export diagnostic log", this::exportLog);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(layout);
        setContentView(scroll);
        updateStatus();
    }

    private Button button(LinearLayout layout, String text, Runnable action) {
        Button button = new Button(this);
        button.setText(text);
        button.setOnClickListener(v -> action.run());
        layout.addView(button);
        return button;
    }

    private File rom(String name) {
        return new File(new File(getFilesDir(), "roms"), name);
    }

    private void updateStatus() {
        status.setText("Connect an external controller. Import your own MAME ROM archives.\n\n"
                + "vr.zip: " + (rom("vr.zip").isFile() ? "imported" : "missing") + "\n"
                + "model1io.zip: " + (rom("model1io.zip").isFile() ? "imported" : "missing"));
        boolean ready = rom("vr.zip").isFile() && rom("model1io.zip").isFile();
        playButton.setEnabled(ready);
        if (ready) playButton.requestFocus();
    }

    private void pick(int request) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, request);
        } catch (ActivityNotFoundException e) {
            showError("No document picker is available on this device.");
        }
    }

    private void exportLog() {
        if (!new File(getFilesDir(), "log.txt").isFile()) {
            showError("There is no diagnostic log yet. Start the game first.");
            return;
        }
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("text/plain");
        intent.putExtra(Intent.EXTRA_TITLE, "virtua-racing-log.txt");
        try {
            startActivityForResult(intent, EXPORT_LOG);
        } catch (ActivityNotFoundException e) {
            showError("No document picker is available on this device.");
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if ((request != PICK_GAME && request != PICK_IO && request != EXPORT_LOG) || result != RESULT_OK) return;
        Uri uri = data == null ? null : data.getData();
        if (uri == null) {
            showError("The document picker did not return a file.");
            return;
        }
        if (request == EXPORT_LOG) {
            runWork(() -> {
                try (InputStream input = new FileInputStream(new File(getFilesDir(), "log.txt"));
                     OutputStream output = getContentResolver().openOutputStream(uri, "wt")) {
                    if (output == null) throw new IOException("Could not open the selected destination.");
                    copyStream(input, output);
                }
            }, "Exporting diagnostic log...", () -> new AlertDialog.Builder(this)
                    .setMessage("Diagnostic log exported.").setPositiveButton("OK", null).show());
            return;
        }
        String name = request == PICK_GAME ? "vr.zip" : "model1io.zip";
        runWork(() -> {
            try (InputStream input = getContentResolver().openInputStream(uri)) {
                if (input == null) throw new IOException("Could not open the selected document.");
                copy(input, rom(name));
            }
        }, "Importing archive...", null);
    }

    private void play() {
        runWork(() -> copyAssets(getAssets(), "assets", new File(getFilesDir(), "assets")),
                "Preparing game assets...", () -> startActivity(new Intent(this, GameActivity.class)));
    }

    private interface Work {
        void run() throws IOException;
    }

    private void runWork(Work work, String progress, Runnable onSuccess) {
        gameButton.setEnabled(false);
        ioButton.setEnabled(false);
        playButton.setEnabled(false);
        exportButton.setEnabled(false);
        status.setText(progress);
        new Thread(() -> {
            try {
                work.run();
                runOnUiThread(() -> {
                    gameButton.setEnabled(true);
                    ioButton.setEnabled(true);
                    exportButton.setEnabled(true);
                    updateStatus();
                    if (onSuccess != null) onSuccess.run();
                });
            } catch (IOException | SecurityException e) {
                runOnUiThread(() -> {
                    gameButton.setEnabled(true);
                    ioButton.setEnabled(true);
                    exportButton.setEnabled(true);
                    updateStatus();
                    showError(e.getMessage() == null ? e.toString() : e.getMessage());
                });
            }
        }, "vr-prepare").start();
    }

    private void showError(String message) {
        new AlertDialog.Builder(this).setTitle("Virtua Racing").setMessage(message)
                .setPositiveButton("OK", null).show();
    }

    private void showLicenses(String... names) {
        try {
            ByteArrayOutputStream text = new ByteArrayOutputStream();
            for (String name : names) {
                try (InputStream input = getAssets().open(name)) {
                    byte[] buffer = new byte[8192];
                    int count;
                    while ((count = input.read(buffer)) != -1) text.write(buffer, 0, count);
                    text.write('\n');
                }
            }
            new AlertDialog.Builder(this).setTitle("Licences")
                    .setMessage(new String(text.toByteArray(), StandardCharsets.UTF_8))
                    .setPositiveButton("OK", null).show();
        } catch (IOException e) {
            showError("Could not read the licences: " + e.getMessage());
        }
    }

    private static void copyAssets(AssetManager assets, String path, File destination) throws IOException {
        String[] children = assets.list(path);
        if (children == null) throw new IOException("Cannot list assets: " + path);
        if (children.length == 0) {
            try (InputStream input = assets.open(path)) {
                copy(input, destination);
            }
        } else {
            for (String child : children) {
                copyAssets(assets, path + "/" + child, new File(destination, child));
            }
        }
    }

    private static void copy(InputStream input, File destination) throws IOException {
        File parent = destination.getParentFile();
        if (!parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Cannot create " + parent);
        }
        File temporary = new File(parent, destination.getName() + ".tmp");
        try {
            try (FileOutputStream output = new FileOutputStream(temporary)) {
                copyStream(input, output);
                output.getFD().sync();
            }
            if (!temporary.renameTo(destination)) throw new IOException("Cannot replace " + destination);
        } finally {
            if (temporary.exists() && !temporary.delete()) {
                android.util.Log.w("VirtuaRacing", "Cannot remove " + temporary);
            }
        }
    }

    private static void copyStream(InputStream input, OutputStream output) throws IOException {
        byte[] buffer = new byte[65536];
        int count;
        while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
    }
}
