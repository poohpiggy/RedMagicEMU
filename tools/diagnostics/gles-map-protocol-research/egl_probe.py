import ctypes as C
e=C.CDLL("libEGL.so.1")
e.eglGetProcAddress.argtypes=[C.c_char_p]; e.eglGetProcAddress.restype=C.c_void_p
def proc(name,ret,args):
 p=e.eglGetProcAddress(name.encode()); assert p, name; return C.CFUNCTYPE(ret,*args)(p)
P=C.c_void_p; I=C.c_int; U=C.c_uint
gpd=proc("eglGetPlatformDisplayEXT",P,[U,P,P]); d=gpd(0x31DD,None,None); assert d
e.eglInitialize.argtypes=[P,C.POINTER(I),C.POINTER(I)]; major=I();minor=I(); assert e.eglInitialize(d,C.byref(major),C.byref(minor))
e.eglBindAPI.argtypes=[U]; assert e.eglBindAPI(0x30A0)
a=(I*9)(0x3033,1,0x3040,0x40,0x3024,8,0x3023,8,0x3038);cfg=P();n=I()
e.eglChooseConfig.argtypes=[P,C.POINTER(I),C.POINTER(P),I,C.POINTER(I)]; assert e.eglChooseConfig(d,a,C.byref(cfg),1,C.byref(n)) and n.value
ca=(I*3)(0x3098,3,0x3038);e.eglCreateContext.argtypes=[P,P,P,C.POINTER(I)];e.eglCreateContext.restype=P;c=e.eglCreateContext(d,cfg,None,ca);assert c
pa=(I*5)(0x3057,1,0x3056,1,0x3038);e.eglCreatePbufferSurface.argtypes=[P,P,C.POINTER(I)];e.eglCreatePbufferSurface.restype=P;s=e.eglCreatePbufferSurface(d,cfg,pa);assert s
e.eglMakeCurrent.argtypes=[P,P,P,P]; assert e.eglMakeCurrent(d,s,s,c)
gs=proc("glGetString",C.c_char_p,[U]);print("EGL",major.value,minor.value,"GL",gs(0x1F02).decode(),"GPU",gs(0x1F01).decode())
