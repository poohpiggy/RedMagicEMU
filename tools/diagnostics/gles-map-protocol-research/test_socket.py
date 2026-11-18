import ctypes as C, socket, struct, threading, queue
exec(open("egl_probe.py").read().replace("pa=(I*5)(0x3057,1,0x3056,1,0x3038)", "pa=(I*5)(0x3057,8,0x3056,8,0x3038)"))
UI=C.c_uint; IP=C.c_ssize_t; UP=C.c_ulonglong; PI=C.POINTER(UI); PW=C.POINTER(UP)
geterr=proc("glGetError",UI,[]);mapgl=proc("glMapBufferRange",P,[UI,IP,IP,UI]);getptr=proc("glGetBufferPointerv",None,[UI,UI,C.POINTER(P)]);fl=proc("glFlushMappedBufferRange",None,[UI,IP,IP]);un=proc("glUnmapBuffer",C.c_ubyte,[UI])
gen=proc("glGenBuffers",None,[I,PI]);bind=proc("glBindBuffer",None,[UI,UI]);alloc=proc("glBufferData",None,[UI,IP,P,UI])
shader=proc("glCreateShader",UI,[UI]);src=proc("glShaderSource",None,[UI,I,C.POINTER(C.c_char_p),C.POINTER(I)]);compile=proc("glCompileShader",None,[UI]);shaderiv=proc("glGetShaderiv",None,[UI,UI,C.POINTER(I)])
program=proc("glCreateProgram",UI,[]);attach=proc("glAttachShader",None,[UI,UI]);link=proc("glLinkProgram",None,[UI]);programiv=proc("glGetProgramiv",None,[UI,UI,C.POINTER(I)]);use=proc("glUseProgram",None,[UI]);location=proc("glGetUniformLocation",I,[UI,C.c_char_p]);uniform=proc("glUniform4f",None,[I,C.c_float,C.c_float,C.c_float,C.c_float])
viewport=proc("glViewport",None,[I,I,I,I]);clearcolor=proc("glClearColor",None,[C.c_float,C.c_float,C.c_float,C.c_float]);clear=proc("glClear",None,[UI]);enable=proc("glEnableVertexAttribArray",None,[UI]);attrib=proc("glVertexAttribPointer",None,[UI,I,UI,C.c_ubyte,I,P]);draw=proc("glDrawArrays",None,[UI,I,I]);readpix=proc("glReadPixels",None,[I,I,I,I,UI,UI,P])
H=C.CDLL("./libpm.so");H.pm_new.argtypes=[P,P,P,P,P];H.pm_new.restype=P;H.pm_ctx_new.argtypes=[P,UP];H.pm_ctx_new.restype=P;H.pm_map.argtypes=[P,UI,IP,IP,UI,PW];H.pm_map.restype=I;H.pm_flush.argtypes=[P,UI,UP,IP,P,IP];H.pm_flush.restype=I;H.pm_unmap.argtypes=[P,UI,UP];H.pm_unmap.restype=I;H.pm_error.argtypes=[P];H.pm_error.restype=UI;H.pm_ctx_free.argtypes=[P];H.pm_free.argtypes=[P];H.pm_free.restype=I
G=C.CDLL("./libgm.so");G.gm_new.argtypes=[I];G.gm_new.restype=P;G.gm_map.argtypes=[P,UI,IP,IP,UI,PW,C.POINTER(P)];G.gm_map.restype=I;G.gm_flush.argtypes=[P,UI,UP,IP,IP];G.gm_flush.restype=I;G.gm_unmap.argtypes=[P,UI,UP];G.gm_unmap.restype=I;G.gm_error.argtypes=[P,PI];G.gm_error.restype=I;G.gm_bind_test.argtypes=[P,UI,UI];G.gm_bind_test.restype=I;G.gm_draw_test.argtypes=[P,UI,UI,PW];G.gm_draw_test.restype=I;G.gm_free.argtypes=[P];G.gm_free.restype=I
request=struct.Struct("<IIQqqI");response=struct.Struct("<iQ");T=0x8F37;R=0x8F36;ready=queue.Queue()
def recvexact(sock,n):
 out=b""
 while len(out)<n:
  x=sock.recv(n-len(out))
  if not x:return None
  out+=x
 return out
def make_shader(kind,text):
 x=shader(kind);q=C.c_char_p(text);src(x,1,C.byref(q),None);compile(x);ok=I();shaderiv(x,0x8B81,C.byref(ok));assert ok.value;return x
def render(a,b):
 v=make_shader(0x8B31,b"#version 300 es\nlayout(location=0) in vec2 p; void main(){gl_Position=vec4(p,0.,1.);}")
 f=make_shader(0x8B30,b"#version 300 es\nprecision mediump float; uniform vec4 color; out vec4 o; void main(){o=color;}")
 prog=program();attach(prog,v);attach(prog,f);link(prog);ok=I();programiv(prog,0x8B82,C.byref(ok));assert ok.value;use(prog);u=location(prog,b"color");assert u>=0
 viewport(0,0,8,8);clearcolor(0,0,0,1);clear(0x4000);enable(0)
 for name,color in ((a,(1.,0.,0.,1.)),(b,(0.,1.,0.,1.))):
  bind(0x8892,name);attrib(0,2,0x1406,0,0,P(8));uniform(u,*color);draw(0x0004,0,3)
 x=(C.c_ubyte*4)();y=(C.c_ubyte*4)();readpix(1,1,1,1,0x1908,0x1401,C.byref(x));readpix(6,6,1,1,0x1908,0x1401,C.byref(y));assert geterr()==0
 return int.from_bytes(bytes(x)+bytes(y),"little")
def server(sock):
 try:
  assert e.eglMakeCurrent(d,s,s,c); h=H.pm_new(*[C.cast(x,P) for x in (geterr,mapgl,getptr,fl,un)]); hc=H.pm_ctx_new(h,1);ids=(UI*2)();gen(2,ids)
  for name in ids:bind(T,name);alloc(T,64,None,0x88E8)
  assert geterr()==0;ready.put((int(ids[0]),int(ids[1])))
  while True:
   raw=recvexact(sock,request.size)
   if raw is None:break
   op,target,id,off,n,flags=request.unpack(raw); value=0; status=0
   if op==1:
    valueout=UP();status=H.pm_map(hc,target,off,n,flags,C.byref(valueout));value=valueout.value
   elif op==2:
    if n<0 or n>4096:raise ValueError("bad flush length")
    data=recvexact(sock,n);assert data is not None;buf=C.create_string_buffer(data);status=H.pm_flush(hc,target,id,off,buf,n)
   elif op==3:status=H.pm_unmap(hc,target,id)
   elif op==4:value=H.pm_error(hc)
   elif op==10:bind(target,flags)
   elif op==11:value=render(target,flags)
   else:status=5
   sock.sendall(response.pack(status,value))
  H.pm_ctx_free(hc);assert H.pm_free(h)==0
 except BaseException as ex:ready.put(ex);raise
 finally:sock.close()
assert e.eglMakeCurrent(d,None,None,None)
a,b=socket.socketpair();th=threading.Thread(target=server,args=(b,),daemon=True);th.start(); names=ready.get(timeout=10);assert not isinstance(names,Exception),names
g=G.gm_new(a.fileno());assert g;assert G.gm_bind_test(g,T,names[0])==0
handles=[];ptrs=[]
for name in names:
 assert G.gm_bind_test(g,T,name)==0;h=UP();p=P();assert G.gm_map(g,T,8,24,0x36,C.byref(h),C.byref(p))==0;handles.append(h.value);ptrs.append(p)
triangles=[(-1.,-1.,1.,-1.,-1.,1.),(1.,1.,-1.,1.,1.,-1.)]
for p,tri in zip(ptrs,triangles):C.memmove(p,struct.pack("<6f",*tri),24)
# id A must not operate against currently bound B; both maps survive.
assert G.gm_flush(g,T,handles[0],0,24)!=0
assert G.gm_bind_test(g,R,names[0])==0;assert G.gm_flush(g,R,handles[0],0,24)==0
assert G.gm_bind_test(g,T,names[1])==0;assert G.gm_flush(g,T,handles[1],0,24)==0
# A prior host GL error remains visible to guest after a successful flush.
assert G.gm_bind_test(g,0xDEADBEEF,0)==0;assert G.gm_flush(g,T,handles[1],0,24)==0;err=UI();assert G.gm_error(g,C.byref(err))==0 and err.value==0x500
for name,h in zip(names,handles):assert G.gm_bind_test(g,T,name)==0 and G.gm_unmap(g,T,h)==0
pixels=UP();assert G.gm_draw_test(g,*names,C.byref(pixels))==0;pix=pixels.value.to_bytes(8,"little");assert pix[:4]==bytes((255,0,0,255)) and pix[4:]==bytes((0,255,0,255)),pix
assert G.gm_free(g)==0;a.close();th.join(timeout=10);assert not th.is_alive();print("SOCKET_GUEST_TO_HOST_DRAW_OK")
