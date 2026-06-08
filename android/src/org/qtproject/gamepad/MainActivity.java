package org.qtproject.gamepad;

import android.os.Bundle;
import android.util.Log;
import android.view.View;

import org.qtproject.qt.android.bindings.QtActivity;

public class MainActivity extends QtActivity
{
    private static final String TAG = "GAMEPAD";

    @Override
    public void onCreate(Bundle savedInstanceState)
    {
        super.onCreate(savedInstanceState);

        // Install on the root content view after Qt finishes loading.
        findViewById(android.R.id.content).post(() -> {
            View view = findViewById(android.R.id.content);

            // Required by Retroid Pocket – gamepad key events need focus.
            view.setFocusable(true);
            view.setFocusableInTouchMode(true);
            view.requestFocus();

            GamepadBridge.install(view);
            Log.i(TAG, "GamepadBridge.install() completed");
        });
    }
}