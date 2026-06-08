package org.qtproject.gamepad;

import android.util.Log;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;

public class GamepadBridge
{
    private static final String TAG = "GAMEPAD";

    public static void install(View view)
    {
        Log.i(TAG, "install() called");

        view.setOnGenericMotionListener(
            (v, event) ->
            {
                float lx =
                    event.getAxisValue(
                        MotionEvent.AXIS_X);

                float ly =
                    event.getAxisValue(
                        MotionEvent.AXIS_Y);

                float rx =
                    event.getAxisValue(
                        MotionEvent.AXIS_Z);

                float ry =
                    event.getAxisValue(
                        MotionEvent.AXIS_RZ);

                float lt =
                    event.getAxisValue(
                        MotionEvent.AXIS_BRAKE);

                float rt =
                    event.getAxisValue(
                        MotionEvent.AXIS_GAS);

                Log.i(TAG,
                    "Axis LX=" + lx +
                    " LY=" + ly +
                    " RX=" + rx +
                    " RY=" + ry +
                    " LT=" + lt +
                    " RT=" + rt);

                nativeAxisChanged(
                    lx, ly, rx, ry, lt, rt);

                return false;
            });

        view.setOnKeyListener(
            (v, keyCode, event) ->
            {
                boolean pressed =
                    event.getAction()
                        == KeyEvent.ACTION_DOWN;

                Log.i(TAG,
                    "Key keyCode=" + keyCode +
                    " pressed=" + pressed);

                nativeButtonChanged(
                    keyCode, pressed);

                return false;
            });
    }

    public static native void nativeAxisChanged(
        float lx, float ly,
        float rx, float ry,
        float lt, float rt);

    public static native void nativeButtonChanged(
        int keyCode, boolean pressed);

}