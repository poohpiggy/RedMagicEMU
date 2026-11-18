package app.aemu.core;

import android.view.Surface;

/** App-private transport. No Surface is accepted until a real renderer exists. */
interface IVulkanSurfaceService {
    const int NOT_READY = 0;
    int status();
    int attachSurface(in Surface surface, long generation);
    int detachSurface(long generation);
}
