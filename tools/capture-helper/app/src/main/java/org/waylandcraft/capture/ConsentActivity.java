// ConsentActivity — minimal MediaProjection consent flow.
package org.waylandcraft.capture;

import android.app.Activity;
import android.os.Bundle;

public class ConsentActivity extends Activity {

    private static final int REQUEST_MEDIA_PROJECTION = 0x574C;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        CaptureService.requestConsent(this, REQUEST_MEDIA_PROJECTION);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MEDIA_PROJECTION && resultCode == RESULT_OK) {
            CaptureService.startWithConsent(this, resultCode, data);
        }
        finish();
    }
}
