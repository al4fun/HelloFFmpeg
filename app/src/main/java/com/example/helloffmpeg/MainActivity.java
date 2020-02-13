package com.example.helloffmpeg;

import androidx.appcompat.app.AppCompatActivity;

import android.os.Bundle;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

public class MainActivity extends AppCompatActivity {
    private static final String TAG = "MainActivity";
    private LinearLayout ll_content;

    static {
        System.loadLibrary("native-lib");
    }

    public native String stringFromJNI();

    public native void helloFFmpeg();

    public native void printFileInfo(String filePath);

    public native void extractAudio(String filePath, String dstFilePath);

    public native void extractVideo(String filePath, String dstFilePath);

    public native int remux(String filePath, String dstFilePath);

    public native int decodeVideo(String filePath, String dstFilePath);

    public native int decodeAudio(String filePath, String dstFilePath);

    public native int encodeAudio(String dstFilePath);

    public native int encodeVideo(String dstFilePath);

    public native int resampleAudio(String dstFilePath);

    public native int scaleVideo(String dstFilePath);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        initView();
    }

    void initView() {
        ll_content = findViewById(R.id.ll_content);

        addButton("show FFmpeg version", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                helloFFmpeg();
            }
        });

        addButton("print file info", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File srcFile = copyFile();
                if (srcFile != null) {
                    printFileInfo("file:" + srcFile.getAbsolutePath());
                }
            }
        });

        addButton("extract aac from video", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File srcFile = copyFile();
                if (srcFile != null) {
                    File dstFile = new File(getCacheDir().getAbsolutePath(), "sintel.aac");
                    if (dstFile.exists()) {
                        dstFile.delete();
                    }
                    extractAudio("file:" + srcFile.getAbsolutePath(), "file:" + dstFile.getAbsolutePath());
                }
            }
        });

        addButton("extract h264 from video", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File srcFile = copyFile();
                if (srcFile != null) {
                    File dstFile = new File(getCacheDir().getAbsolutePath(), "sintel.h264");
                    if (dstFile.exists()) {
                        dstFile.delete();
                    }
                    extractVideo("file:" + srcFile.getAbsolutePath(), "file:" + dstFile.getAbsolutePath());
                }
            }
        });

        addButton("remux", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File srcFile = copyFile();
                if (srcFile != null) {
                    File dstFile = new File(getCacheDir().getAbsolutePath(), "sintel.flv");
                    if (dstFile.exists()) {
                        dstFile.delete();
                    }
                    remux("file:" + srcFile.getAbsolutePath(), "file:" + dstFile.getAbsolutePath());
                }
            }
        });

        addButton("decode h264 video to yuv", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File src = new File(getCacheDir().getAbsolutePath(), "sintel.h264");
                File dst = new File(getCacheDir().getAbsolutePath(), "sintel_frame");
                decodeVideo(src.getAbsolutePath(), dst.getAbsolutePath());
            }
        });

        addButton("decode aac audio to pcm", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File src = new File(getCacheDir().getAbsolutePath(), "sintel.aac");
                File dst = new File(getCacheDir().getAbsolutePath(), "sintel.pcm");
                decodeAudio(src.getAbsolutePath(), dst.getAbsolutePath());
            }
        });

        addButton("encode mp2 audio", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File dst = new File(getCacheDir().getAbsolutePath(), "new.mp2");
                encodeAudio(dst.getAbsolutePath());
            }
        });

        addButton("encode h264 video", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File dst = new File(getCacheDir().getAbsolutePath(), "new");
                encodeVideo(dst.getAbsolutePath());
            }
        });

        addButton("resample audio", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File dst = new File(getCacheDir().getAbsolutePath(), "resampled");
                resampleAudio(dst.getAbsolutePath());
            }
        });

        addButton("scale video", new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File dst = new File(getCacheDir().getAbsolutePath(), "scaled");
                scaleVideo(dst.getAbsolutePath());
            }
        });
    }

    /**
     * 将asset目录中的媒体文件拷贝到sd卡中
     */
    File copyFile() {
        String fileName = "sintel.mp4";

        InputStream inputStream = null;
        FileOutputStream fileOutputStream = null;
        try {
            //将asset中的文件复制到sd卡中
            inputStream = getAssets().open(fileName);
            File file = new File(getCacheDir().getAbsolutePath(), fileName);
            if (file.exists()) {
                file.delete();
            }
            fileOutputStream = new FileOutputStream(file);
            byte[] buffer = new byte[8192];
            int byteCount;
            while ((byteCount = inputStream.read(buffer)) != -1) {
                fileOutputStream.write(buffer, 0, byteCount);
            }
            fileOutputStream.flush();

            return file;
        } catch (Exception e) {
            Log.e(TAG, e.toString());
            return null;
        } finally {
            try {
                if (inputStream != null) inputStream.close();
                if (fileOutputStream != null) fileOutputStream.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    private void addButton(String name, View.OnClickListener lisener) {
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(20, 20, 20, 20);

        Button button = new Button(this);
        button.setPadding(0, 20, 0, 20);
        button.setGravity(Gravity.CENTER);
        button.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 16);
        button.setText(name);
        button.setAllCaps(false);
        button.setOnClickListener(lisener);

        ll_content.addView(button, lp);
    }
}
