import ctypes as C, socket,struct,threading
L=C.CDLL("./libgm.so");P=C.c_void_p;I=C.c_int;U=C.c_uint;Q=C.c_ulonglong;S=C.c_ssize_t
L.gm_new.argtypes=[I];L.gm_new.restype=P;L.gm_map.argtypes=[P,U,S,S,U,C.POINTER(Q),C.POINTER(P)];L.gm_map.restype=I;L.gm_flush.argtypes=[P,U,Q,S,S];L.gm_flush.restype=I;L.gm_unmap.argtypes=[P,U,Q];L.gm_unmap.restype=I;L.gm_free.argtypes=[P];L.gm_free.restype=I;L.gm_bind_test.argtypes=[P,U,U];L.gm_bind_test.restype=I
REQ=struct.Struct("<IIQqqI");RESP=struct.Struct("<iQ")
def read_exact(s,n):
 b=b""
 while len(b)<n:
  p=s.recv(n-len(b))
  if not p:return None
  b+=p
 return b
def make_server(kind):
 a,b=socket.socketpair()
 def server():
  q=read_exact(b,REQ.size);assert q
  if kind=="short-map":b.sendall(RESP.pack(0,99)[:5]);b.close();return
  if kind=="bad-status":b.sendall(RESP.pack(55,0));b.close();return
  b.sendall(RESP.pack(0,99));q=read_exact(b,REQ.size);assert q;op,target,id,off,n,flags=REQ.unpack(q);assert op==3
  # Assume the host performed unmap, but the response is lost.
  b.sendall(RESP.pack(0,0)[:4]);b.close()
 t=threading.Thread(target=server,daemon=True);t.start();return a,t
for kind in ("short-map","bad-status","lost-unmap"):
 a,t=make_server(kind);g=L.gm_new(a.fileno());h=Q();p=P();rc=L.gm_map(g,0x8F37,0,16,0x36,C.byref(h),C.byref(p))
 if kind=="lost-unmap":
  assert rc==0 and h.value==99 and p.value;assert L.gm_unmap(g,0x8F37,h.value)!=0;assert L.gm_free(g)!=0
 else:
  assert rc!=0 and h.value==0 and not p.value;assert L.gm_bind_test(g,0x8F37,1)!=0;assert L.gm_free(g)==0
 a.close();t.join(timeout=3);assert not t.is_alive()
print("SOCKET_FAILURES_NO_FALSE_SUCCESS_OK")
