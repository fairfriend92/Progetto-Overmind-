package com.example.overmind;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.os.CountDownTimer;
import android.support.v4.content.LocalBroadcastManager;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.EditText;

import java.io.IOException;
import java.io.InputStream;
import java.net.Socket;
import java.util.Scanner;
import java.util.concurrent.ExecutionException;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class MainActivity extends AppCompatActivity {

    /**
     * Class which provides GPU info
     */

    private class MyGLRenderer implements GLSurfaceView.Renderer {

        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            // Get various GPU and GL information
            Log.d("GL info", "gl renderer: " + gl.glGetString(GL10.GL_RENDERER));
            Log.d("GL info", "gl vendor: " + gl.glGetString(GL10.GL_VENDOR));
            Log.d("GL info", "gl version: " + gl.glGetString(GL10.GL_VERSION));
            Log.d("GL info", "gl extensions: " + gl.glGetString(GL10.GL_EXTENSIONS));

            // Store the needed GPU info in the preferences
            SharedPreferences.Editor editor = prefs.edit();
            editor.putString("VENDOR", gl.glGetString(GL10.GL_VENDOR));
            editor.putString("RENDERER", gl.glGetString(GL10.GL_RENDERER));
            editor.apply();

            // Set the background frame color
            GLES20.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        }

        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height) {
            GLES20.glViewport(0, 0, width, height);
        }

        @Override
        public void onDrawFrame(GL10 gl) {
            // Redraw background color
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        }
    }

    /**
     * OpenGL surface view called at app startup to retrieve GPU info
     */

    class MyGLSurfaceView extends GLSurfaceView {
        public MyGLRenderer mRenderer;

        public MyGLSurfaceView(Context context) {
            super(context);

            // Create an OpenGL ES 2.0 context
            setEGLContextClientVersion(2);

            mRenderer = new MyGLRenderer();

            // Set the Renderer for drawing on the GLSurfaceView
            setRenderer(mRenderer);
        }
    }

    // Used to store GPU info by the renderer class
    private static SharedPreferences prefs;

    public static Socket thisClient;

    static String serverIP;
    static String renderer;

    EditText editText = null;

    public void lookUpServerIP(View view) {
        String ip = null;
        LookUpServerIP lookUpServerIP = new LookUpServerIP();
        lookUpServerIP.execute();
        try {
            ip = lookUpServerIP.get();
        } catch (InterruptedException | ExecutionException e) {
            String stackTrace = Log.getStackTraceString(e);
            Log.e("MainActivity", stackTrace);
        }
        assert ip != null;
        editText.setText(ip);
    }

    public static boolean IpCheckerFailed = false;

    public void confirmServerIP(View view) {

        assert editText != null;
        serverIP = editText.getText().toString();

        // OpenGL surface view
        MyGLSurfaceView mGlSurfaceView = new MyGLSurfaceView(this);
        // Set on display the OpenGL surface view in order to call the OpenGL renderer and retrieve the GPU info
        setContentView(mGlSurfaceView);

        // Use this countdown timer to give enough time to the renderer to retrieve the info
        new CountDownTimer(1000, 500) {
            public void onTick(long millisUntilFinished) {
            }

            public void onFinish() {

                // Get the GPU model to set the number of neurons of the local network
                SharedPreferences prefs = getSharedPreferences("GPUinfo", Context.MODE_PRIVATE);
                renderer = prefs.getString("RENDERER", null);

                // Load the appropriate OpenCL library based on the GPU vendor
                loadGLLibrary();

                // Create and launch the AsyncTask to retrieve the global IP of the device and to send the info
                // of the local network to the Overmind server
                IpChecker ipChecker = new IpChecker();
                ipChecker.execute(getApplicationContext());

                // Get from the AsyncTask the struct holding all the info regardinf the local network
                try {
                    thisClient = ipChecker.get();
                } catch (InterruptedException | ExecutionException e) {
                    String stackTrace = Log.getStackTraceString(e);
                    Log.e("MainActivity", stackTrace);
                }

                if (IpCheckerFailed) {
                    IpCheckerFailed = false;
                    android.support.v4.app.DialogFragment dialogFragment = new ConnectionFailedDialogFragment();
                    Bundle args = new Bundle();
                    args.putInt("ErrorNumber", 0);
                    dialogFragment.setArguments(args);
                    dialogFragment.show(getSupportFragmentManager(), "Connection failed");
                    setContentView(R.layout.pre_connection);
                    editText = (EditText) findViewById(R.id.edit_ip);
                } else {
                    // Now that the GPU info are available display the proper application layout
                    setContentView(R.layout.activity_main);
                    startSimulation();
                }

                assert thisClient != null;

            }
        }.start();
    }

    /**
     * Handler for received Intents: called whenever an Intent  with action named
     * "Error message" is broadcast.
     */

    int errorNumber = 0;

    private BroadcastReceiver mMessageReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {

            // Get data included in the Intent
            errorNumber = intent.getIntExtra("ErrorNumber", errorNumber);
            Bundle args = new Bundle();
            android.support.v4.app.DialogFragment dialogFragment = new ConnectionFailedDialogFragment();
            args.putInt("ErrorNumber", errorNumber);
            dialogFragment.setArguments(args);
            dialogFragment.show(getSupportFragmentManager(), "Connection failed");
            setContentView(R.layout.pre_connection);
            editText = (EditText) findViewById(R.id.edit_ip);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Used to get the GPU info stored by the OpenGL renderer
        prefs = this.getSharedPreferences("GPUinfo", Context.MODE_PRIVATE);
        setContentView(R.layout.pre_connection);
        editText = (EditText) findViewById(R.id.edit_ip);

        /**
         * Register an observer (mMessageReceiver) to receive Intents with actions named
         * "ErrorMessage"
         */
        LocalBroadcastManager.getInstance(this).registerReceiver(mMessageReceiver,
                new IntentFilter("ErrorMessage"));
    }

    @Override
    public void onDestroy() {
        // Unregister the receiver since the service is about to be closed
        LocalBroadcastManager.getInstance(this).unregisterReceiver(mMessageReceiver);
        SimulationService.shutDown();
        super.onDestroy();
    }

    /**
     * Load the proper OpenGL library based on GPU vendor info provided by the OpenGL renderer
     */

    public void loadGLLibrary() {
        SharedPreferences prefs = getSharedPreferences("GPUinfo", Context.MODE_PRIVATE);
        String vendor = prefs.getString("VENDOR", null);
        assert vendor != null;
        switch (vendor) {
            case "ARM":
                try {
                    System.loadLibrary("libGLES_mali.so");
                } catch (UnsatisfiedLinkError linkError) {
                    Log.e("Unsatisfied link", "libGLES_mali.so not found");
                }
                break;
            // TODO default means no OpenCL support, exit application.
            default:
                try {
                    System.loadLibrary("libOpenCL.so");
                } catch (UnsatisfiedLinkError linkError) {
                    Log.e("Unsatisfied link", "libGLES_mali.so not found");
                }
        }
    }

    static {
        System.loadLibrary("hello-world");
    }

    public InputStream getInputStream(String kernelName) {
        try {
            return getAssets().open(kernelName);
        } catch (IOException ioException) {
            Log.e("IO exception", "Cannot retrieve OpenCL kernel");
            return null;
        }
    }

    static String loadKernelFromAsset(InputStream inputStream) {
        Scanner scanner = new Scanner(inputStream).useDelimiter("\\A");
        return scanner.hasNext() ? scanner.next() : " ";
    }

    public void startSimulation() {

        // The SimulationService Intent
        Intent simulationIntent = new Intent(MainActivity.this, SimulationService.class);

        String kernel;

        // TODO it would probably be better to get the needed info from the OpenCL context using native method calls

        switch (renderer) {
            case "Mali-T720":
                // The string used to hold the .cl kernel file
                kernel = loadKernelFromAsset(getInputStream("kernel_vec4.cl"));
                break;
            default:
                kernel = loadKernelFromAsset(getInputStream("kernel.cl"));
                break;
        }

        // Put the string holding the kernel in the simulation Intent
        simulationIntent.putExtra("Kernel", kernel);

        SimulationService.shutdown = false;

        // Start the service
        this.startService(simulationIntent);

    }

}


