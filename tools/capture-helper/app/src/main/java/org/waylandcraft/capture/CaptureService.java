// ============================================================================
//  WaylandCraft-BE — capture-helper (companion app, frame streaming service)
//  app/src/main/java/org/waylandcraft/capture/CaptureService.java
//
//  Streams the device display (or a specific app's surface region when
//  launched through the intent bridge) to the mod's CaptureService on
//  127.0.0.1:7232 (CaptureProtocol v1 — see docs/PROTOCOL.md).
//
//  Flow:
//    1. User grants MediaProjection consent (foreground service, required
//       on Android 10+).
//    2. VirtualDisplay is created sized to the target app's window.
//    3. ImageReader delivers RGBA_8888 frames at ~30 fps.
//    4. Each frame is sent with the WLCF header + payload.
// ============================================================================
package org.waylandcraft.capture;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.util.DisplayMetrics;
import android.view.WindowManager;

import java.io.IOException;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.Socket;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public class CaptureService extends Service {

    public static final int MSG_FRAME = 1;
    public static final int MSG_ICON = 2;
    public static final int MSG_HELLO = 3;
    public static final int MSG_BYE = 4;

    private static final int GAME_PORT = 7232;
    private static final String CHANNEL_ID = "wlc-capture";
    private static final int NOTIFICATION_ID = 0x574C;

    private MediaProjection projection;
    private VirtualDisplay virtualDisplay;
    private ImageReader imageReader;
    private Socket socket;
    private OutputStream out;
    private HandlerThread frameThread;
    private Handler frameHandler;
    private volatile boolean streaming = false;

    public static void requestConsent(Activity activity, int requestCode) {
        MediaProjectionManager mgr = (MediaProjectionManager)
                activity.getSystemService(Context.MEDIA_PROJECTION_SERVICE);
        activity.startActivityForResult(mgr.createScreenCaptureIntent(), requestCode);
    }

    public static void startWithConsent(Context ctx, int resultCode, Intent data) {
        Intent svc = new Intent(ctx, CaptureService.class)
                .putExtra("resultCode", resultCode)
                .putExtra("data", data);
        ctx.startForegroundService(svc);
    }

    @Override
    public void onCreate() {
        super.onCreate();
        NotificationChannel ch = new NotificationChannel(
                CHANNEL_ID, "WaylandCraft capture", NotificationManager.IMPORTANCE_LOW);
        ((NotificationManager) getSystemService(NOTIFICATION_SERVICE))
                .createNotificationChannel(ch);
        Notification n = new Notification.Builder(this, CHANNEL_ID)
                .setContentTitle("WaylandCraft-BE")
                .setContentText("Streaming app window into Minecraft")
                .setSmallIcon(android.R.drawable.ic_menu_view)
                .build();
        startForeground(NOTIFICATION_ID, n);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        int resultCode = intent.getIntExtra("resultCode", Activity.RESULT_CANCELED);
        Intent data = intent.getParcelableExtra("data");
        MediaProjectionManager mgr = (MediaProjectionManager)
                getSystemService(Context.MEDIA_PROJECTION_SERVICE);
        projection = mgr.getMediaProjection(resultCode, data);
        projection.registerCallback(new MediaProjection.Callback() {
            @Override public void onStop() { stopStreaming(); }
        }, null);
        connectAndStream();
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        stopStreaming();
        super.onDestroy();
    }

    private void connectAndStream() {
        new Thread(() -> {
            try {
                socket = new Socket(InetAddress.getByAddress(new byte[]{127, 0, 0, 1}),
                                    GAME_PORT);
                out = socket.getOutputStream();
                sendHeader(MSG_HELLO, 0, 0, 0, 0, 0, 0);
                startVirtualDisplay();
                streaming = true;
            } catch (IOException e) {
                stopSelf();
            }
        }, "wlc-connect").start();
    }

    private void startVirtualDisplay() {
        DisplayMetrics dm = new DisplayMetrics();
        WindowManager wm = (WindowManager) getSystemService(WINDOW_SERVICE);
        wm.getDefaultDisplay().getRealMetrics(dm);
        int width = Math.min(dm.widthPixels, 1280);
        int height = dm.heightPixels * width / dm.widthPixels;

        frameThread = new HandlerThread("wlc-frames");
        frameThread.start();
        frameHandler = new Handler(frameThread.getLooper());

        imageReader = ImageReader.newInstance(width, height, PixelFormat.RGBA_8888, 4);
        imageReader.setOnImageAvailableListener(reader -> {
            Image img = null;
            try {
                img = reader.acquireLatestImage();
                if (img == null || !streaming) return;
                sendFrame(img);
            } catch (IOException e) {
                streaming = false;
            } finally {
                if (img != null) img.close();
            }
        }, frameHandler);

        virtualDisplay = projection.createVirtualDisplay(
                "waylandcraft-be", width, height, dm.densityDpi,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                imageReader.getSurface(), null, frameHandler);
    }

    private void sendFrame(Image img) throws IOException {
        Image.Plane plane = img.getPlanes()[0];
        ByteBuffer buf = plane.getBuffer();
        int rowStride = plane.getRowStride();
        int width = img.getWidth();
        int height = img.getHeight();
        byte[] pixels = new byte[buf.remaining()];
        buf.get(pixels);
        sendHeader(MSG_FRAME, 1, width, height, rowStride, 0, pixels.length);
        out.write(pixels);
        out.flush();
    }

    private void sendHeader(int msgType, int payloadId, int width, int height,
                            int stride, int format, int payloadLen) throws IOException {
        ByteBuffer h = ByteBuffer.allocate(36).order(ByteOrder.LITTLE_ENDIAN);
        h.putInt(0x574C4346);
        h.putInt(1);
        h.putInt(msgType);
        h.putInt(payloadId);
        h.putInt(width);
        h.putInt(height);
        h.putInt(stride);
        h.putInt(format);
        h.putInt(payloadLen);
        out.write(h.array());
    }

    private void stopStreaming() {
        streaming = false;
        if (virtualDisplay != null) { virtualDisplay.release(); virtualDisplay = null; }
        if (imageReader != null) { imageReader.close(); imageReader = null; }
        if (projection != null) { projection.stop(); projection = null; }
        if (frameThread != null) { frameThread.quitSafely(); frameThread = null; }
        try {
            if (out != null) sendHeader(MSG_BYE, 0, 0, 0, 0, 0, 0);
            if (socket != null) socket.close();
        } catch (IOException ignored) { }
        stopSelf();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }
}
