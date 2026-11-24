package recomp.vr1.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Insets;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.Build;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

import java.util.ArrayList;

final class TouchControls extends View {
    private static final int SOUTH = 0, EAST = 1, WEST = 2, NORTH = 3;
    private static final int BACK = 4, START = 6, DPAD_UP = 11;
    private static final int STICK = -1, THROTTLE = -2, LEFT_TRIGGER = -3;
    private static final int RIGHT_TRIGGER = -4, REAR = -5;
    private static final String GAME = BuildConfig.GAME_ARCHIVE;

    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ArrayList<Control> controls = new ArrayList<>();
    private final SparseArray<Pointer> pointers = new SparseArray<>();
    private boolean active, menu, pcMode = true;
    private float throttle = GAME.equals("swa.zip") ? 0.5f : 0;
    private int insetLeft, insetTop, insetRight, insetBottom;

    private static final class Control {
        final String label;
        final int action;
        final float x, y, radius, height;

        Control(String label, int action, float x, float y, float radius, float height) {
            this.label = label;
            this.action = action;
            this.x = x;
            this.y = y;
            this.radius = radius;
            this.height = height;
        }

        boolean contains(float px, float py) {
            if (action == THROTTLE)
                return Math.abs(px - x) <= radius && Math.abs(py - y) <= height / 2;
            return Math.hypot(px - x, py - y) <= radius * (action == STICK ? 1.15f : 1.0f);
        }
    }

    private static final class Pointer {
        final Control control;
        float x, y;
        Pointer(Control control, float x, float y) {
            this.control = control;
            this.x = x;
            this.y = y;
        }
    }

    TouchControls(Context context) {
        super(context);
        setContentDescription("Touch game controls");
    }

    private static native void nativeSetState(boolean enabled, int buttons,
            float lx, float ly, float rx, float ry, float lt, float rt);

    void setActive(boolean enabled) {
        if (active == enabled) return;
        active = enabled;
        pointers.clear();
        if (!enabled) throttle = GAME.equals("swa.zip") ? 0.5f : 0;
        sendState();
        invalidate();
    }

    void setUiState(boolean open, boolean pcMode) {
        if (menu == open && this.pcMode == pcMode) return;
        menu = open;
        this.pcMode = pcMode;
        pointers.clear();
        layoutControls();
        sendState();
        invalidate();
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        pointers.clear();
        layoutControls();
        sendState();
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        int left, top, right, bottom;
        if (Build.VERSION.SDK_INT >= 30) {
            Insets safe = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            left = safe.left; top = safe.top; right = safe.right; bottom = safe.bottom;
        } else {
            left = insets.getSystemWindowInsetLeft(); top = insets.getSystemWindowInsetTop();
            right = insets.getSystemWindowInsetRight(); bottom = insets.getSystemWindowInsetBottom();
            if (Build.VERSION.SDK_INT >= 28 && insets.getDisplayCutout() != null) {
                left = Math.max(left, insets.getDisplayCutout().getSafeInsetLeft());
                top = Math.max(top, insets.getDisplayCutout().getSafeInsetTop());
                right = Math.max(right, insets.getDisplayCutout().getSafeInsetRight());
                bottom = Math.max(bottom, insets.getDisplayCutout().getSafeInsetBottom());
            }
        }
        if (left != insetLeft || top != insetTop || right != insetRight || bottom != insetBottom) {
            insetLeft = left; insetTop = top; insetRight = right; insetBottom = bottom;
            pointers.clear();
            layoutControls();
            sendState();
            invalidate();
        }
        return super.onApplyWindowInsets(insets);
    }

    private void add(String label, int action, float x, float y, float radius) {
        controls.add(new Control(label, action, x, y, radius, 0));
    }

    private void layoutControls() {
        controls.clear();
        float w = getWidth() - insetLeft - insetRight, h = getHeight() - insetTop - insetBottom;
        if (menu || w == 0 || h == 0) return;
        float left = insetLeft, right = left + w, top = insetTop;
        float r = h * 0.065f;
        add("STICK", STICK, left + h * 0.27f, top + h * 0.78f, h * 0.17f);
        float utilityRadius = r * 0.8f;
        float utilityOffset = h * (pcMode ? 0.085f : 0.17f);
        // centred on the screen, not the safe area: a one-sided camera cutout would shift the group
        float centre = getWidth() * 0.5f;
        add("START", START, centre - utilityOffset, top + h * 0.09f, utilityRadius);
        if (!pcMode) add("COIN", DPAD_UP, centre, top + h * 0.09f, utilityRadius);
        add("MENU", BACK, centre + utilityOffset, top + h * 0.09f, utilityRadius);

        if (GAME.equals("vr.zip")) {
            action("GAS", RIGHT_TRIGGER, 0, 0, right, top, h, r);
            action("BRAKE", LEFT_TRIGGER, 1, 0, right, top, h, r);
            action("SHIFT+", SOUTH, 0, 1, right, top, h, r);
            action("SHIFT-", EAST, 1, 1, right, top, h, r);
            action("VIEW", NORTH, 0, 2, right, top, h, r);
            action("REAR", REAR, 1, 2, right, top, h, r);
        } else {
            controls.add(new Control("THROTTLE", THROTTLE, right - h * 0.48f,
                    top + h * 0.72f, h * 0.055f, h * 0.38f));
            if (GAME.equals("swa.zip")) {
                action("LASER", RIGHT_TRIGGER, 0, 0, right, top, h, r);
                action("TORPEDO", LEFT_TRIGGER, 1, 0, right, top, h, r);
                action("VIEW", NORTH, 0, 1, right, top, h, r);
            } else {
                action("GUN", SOUTH, 0, 0, right, top, h, r);
                action("MISSILE", EAST, 1, 0, right, top, h, r);
                action("SMOKE", WEST, 0, 1, right, top, h, r);
                action("VIEW", NORTH, 1, 1, right, top, h, r);
            }
        }
    }

    private void action(String label, int output, int column, int row, float right, float top, float h, float radius) {
        add(label, output, right - h * (0.12f + column * 0.15f),
                top + h * (0.88f - row * 0.15f), radius);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        for (Control c : controls) {
            paint.setStyle(Paint.Style.FILL);
            paint.setColor(Color.argb(115, 15, 23, 35));
            if (c.action == THROTTLE) {
                canvas.drawRoundRect(c.x - c.radius, c.y - c.height / 2,
                        c.x + c.radius, c.y + c.height / 2, c.radius, c.radius, paint);
                paint.setColor(Color.argb(190, 220, 235, 250));
                canvas.drawCircle(c.x, c.y + c.height * (0.5f - throttle), c.radius * 0.65f, paint);
            } else {
                canvas.drawCircle(c.x, c.y, c.radius, paint);
            }
            paint.setColor(Color.argb(180, 220, 235, 250));
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(Math.max(2, getHeight() * 0.003f));
            if (c.action == THROTTLE)
                canvas.drawRoundRect(c.x - c.radius, c.y - c.height / 2,
                        c.x + c.radius, c.y + c.height / 2, c.radius, c.radius, paint);
            else
                canvas.drawCircle(c.x, c.y, c.radius, paint);
            paint.setStyle(Paint.Style.FILL);
            paint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setTextSize(c.action == THROTTLE ? getHeight() * 0.024f
                    : Math.min(getHeight() * 0.032f, c.radius * 1.5f / Math.max(1, c.label.length() * 0.55f)));
            if (c.action == THROTTLE)
                canvas.drawText(c.label, c.x, c.y - c.height / 2 - getHeight() * 0.015f, paint);
            else if (c.action != STICK)
                canvas.drawText(c.label, c.x, c.y - (paint.ascent() + paint.descent()) / 2, paint);
        }
        for (int i = 0; i < pointers.size(); i++) {
            Pointer p = pointers.valueAt(i);
            if (p.control.action != STICK) continue;
            float dx = p.x - p.control.x, dy = p.y - p.control.y;
            float scale = Math.min(1, p.control.radius / Math.max(1, (float) Math.hypot(dx, dy)));
            paint.setColor(Color.argb(190, 220, 235, 250));
            canvas.drawCircle(p.control.x + dx * scale, p.control.y + dy * scale,
                    p.control.radius * 0.28f, paint);
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (!active || menu) return false;
        int action = event.getActionMasked(), index = event.getActionIndex();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            float x = event.getX(index), y = event.getY(index);
            for (Control c : controls) {
                if (!c.contains(x, y)) continue;
                boolean used = false;
                for (int i = 0; i < pointers.size(); i++) used |= pointers.valueAt(i).control == c;
                if (!used) pointers.put(event.getPointerId(index), new Pointer(c, x, y));
                break;
            }
            if (action == MotionEvent.ACTION_DOWN && pointers.size() == 0) return false;
        } else if (action == MotionEvent.ACTION_CANCEL) {
            pointers.clear();
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            pointers.remove(event.getPointerId(index));
        }
        for (int i = 0; i < event.getPointerCount(); i++) {
            Pointer p = pointers.get(event.getPointerId(i));
            if (p == null) continue;
            p.x = event.getX(i);
            p.y = event.getY(i);
            if (p.control.action == THROTTLE)
                throttle = Math.max(0, Math.min(1, 0.5f - (p.y - p.control.y) / p.control.height));
        }
        sendState();
        invalidate();
        return true;
    }

    private void sendState() {
        int buttons = 0;
        float lx = 0, ly = 0, ry = 0, lt = 0, rt = 0;
        if (active && !menu) {
            if (GAME.equals("swa.zip")) ry = 1 - 2 * throttle;
            else if (GAME.equals("wingwar.zip")) rt = throttle;
        }
        for (int i = 0; active && i < pointers.size(); i++) {
            Pointer p = pointers.valueAt(i);
            Control c = p.control;
            if (c.action == STICK) {
                float x = (p.x - c.x) / c.radius, y = (p.y - c.y) / c.radius;
                float scale = Math.max(1, (float) Math.hypot(x, y));
                lx = x / scale;
                ly = y / scale;
            } else if (c.action != THROTTLE && c.contains(p.x, p.y)) {
                if (c.action >= 0) buttons |= 1 << c.action;
                else if (c.action == LEFT_TRIGGER) lt = 1;
                else if (c.action == RIGHT_TRIGGER) rt = 1;
                else if (c.action == REAR) ry = 1;
            }
        }
        nativeSetState(active, buttons, lx, ly, 0, ry, lt, rt);
    }
}
