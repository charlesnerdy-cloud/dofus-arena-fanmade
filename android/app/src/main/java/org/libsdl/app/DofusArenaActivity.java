package org.libsdl.app;

import android.os.Bundle;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public final class DofusArenaActivity extends SDLActivity {
    private static final String ASSET_VERSION = "1.2";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        try {
            File marker = new File(getFilesDir(), ".assets-" + ASSET_VERSION);
            if (!marker.isFile()) {
                copyAssetTree("assets", new File(getFilesDir(), "assets"));
                copySystemFont(new File(getFilesDir(), "assets/font.ttf"));
                if (!marker.createNewFile()) {
                    throw new IOException("Could not record installed asset version");
                }
            }
        } catch (IOException error) {
            throw new IllegalStateException("Could not install game assets", error);
        }
        super.onCreate(savedInstanceState);
    }

    private void copySystemFont(File destination) throws IOException {
        File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Could not create " + parent);
        }
        try (InputStream input =
                     new FileInputStream("/system/fonts/Roboto-Regular.ttf");
             OutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[16384];
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
            }
        }
    }

    private void copyAssetTree(String assetPath, File destination)
            throws IOException {
        String[] children = getAssets().list(assetPath);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) {
                throw new IOException("Could not create " + destination);
            }
            for (String child : children) {
                copyAssetTree(assetPath + "/" + child,
                              new File(destination, child));
            }
            return;
        }

        File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Could not create " + parent);
        }
        try (InputStream input = getAssets().open(assetPath);
             OutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[16384];
            int count;
            while ((count = input.read(buffer)) != -1) {
                output.write(buffer, 0, count);
            }
        }
    }
}
