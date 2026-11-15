package app.aemu.stub;
final class WifiDnsSession {
    private final long epoch;
    private final WifiDnsWire wire;
    private final WifiDnsGuard guard;
    private final WifiDnsGuard.Source source;
    WifiDnsSession(long epoch, WifiDnsGuard.Source source) {
        this.epoch=epoch; this.wire=new WifiDnsWire(epoch); this.guard=new WifiDnsGuard(source); this.source=source;
    }
    synchronized byte[] receive(byte[] frame) {
        WifiDnsWire.Request request=wire.read(frame);
        if (request==null) return null;
        boolean result=request.operation==1 ? guard.begin(request.token, request.netId)
                : request.operation==2 ? guard.finish(request.token) : source.report(request.netId==1);
        return WifiDnsWire.ack(epoch, request.sequence, result);
    }
}
