package app.aemu.importer
import org.junit.Assert.*
import org.junit.Test
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.ByteOrder

class StrictExt4Test {
 private class Source(val bytes: ByteArray): RandomSource {
  override val size=bytes.size.toLong()
  override fun read(pos:Long,dst:ByteBuffer){if(pos<0||pos>size||dst.remaining().toLong()>size-pos)throw IOException("outside");dst.put(bytes,pos.toInt(),dst.remaining())}
 }
 private fun image():ByteArray {
  val a=ByteArray(32768);val b=ByteBuffer.wrap(a).order(ByteOrder.LITTLE_ENDIAN);val s=1024
  b.putInt(s,16);b.putInt(s+4,8);b.putInt(s+20,0);b.putInt(s+24,2);b.putInt(s+32,8);b.putInt(s+40,16);b.putShort(s+56,0xef53.toShort());b.putInt(s+76,1);b.putShort(s+88,128);b.putInt(s+96,2)
  b.putInt(4096+8,2);val root=8192+128;b.putShort(root,0x41ed);b.putInt(root+4,12);b.putInt(root+40,3)
  b.putInt(12288,3);b.putShort(12292,12);b.put(12294,4);b.put(12295,1);System.arraycopy("file".toByteArray(),0,a,12296,4)
  return a
 }
 @Test fun rejectsInvalidSuperblockGeometry(){val a=image();ByteBuffer.wrap(a).order(ByteOrder.LITTLE_ENDIAN).putInt(1048,31);assertThrows(IOException::class.java){Ext4Reader(Source(a), strict=true)}}
 @Test fun rejectsInvalidDirectoryNames(){val a=image();a[12297]='/'.code.toByte();assertThrows(IOException::class.java){Ext4Reader(Source(a), strict=true).walk { _,_-> }}}
 @Test fun rejectsMalformedDirectoryRecord(){val a=image();ByteBuffer.wrap(a).order(ByteOrder.LITTLE_ENDIAN).putShort(12292,2);assertThrows(IOException::class.java){Ext4Reader(Source(a), strict=true).walk { _,_-> }}}
 @Test fun rejectsOutOfRangeInode(){assertThrows(IOException::class.java){Ext4Reader(Source(image()), strict=true).inode(99)}}
 @Test fun cancelledWalkStops(){assertThrows(IOException::class.java){Ext4Reader(Source(image()), strict=true,cancelled={true}).walk { _,_-> }}}
 @Test fun rejectsInvalidExtentTreeMagic(){
  val node=Ext4Reader.Node(3,0x81a4,0,0,4096,0x80000,ByteArray(60),0)
  assertThrows(IOException::class.java){Ext4Reader(Source(image()),strict=true).copy(node,java.io.ByteArrayOutputStream())}
 }
 @Test fun rejectsExtentOutsideImage(){
  val block=ByteArray(60);val b=ByteBuffer.wrap(block).order(ByteOrder.LITTLE_ENDIAN)
  b.putShort(0,0xf30a.toShort());b.putShort(2,1);b.putShort(6,0);b.putInt(12,0);b.putShort(16,1);b.putInt(20,8)
  val node=Ext4Reader.Node(3,0x81a4,0,0,4096,0x80000,block,0)
  assertThrows(IOException::class.java){Ext4Reader(Source(image()),strict=true).copy(node,java.io.ByteArrayOutputStream())}
 }
 @Test fun rejectsInodeTableAddressOverflow(){
  val a=image();val b=ByteBuffer.wrap(a).order(ByteOrder.LITTLE_ENDIAN)
  b.putInt(1024+96,0x82);b.putShort(1024+254,64);b.putInt(4096+8,0);b.putInt(4096+40,0x100000)
  assertThrows(IOException::class.java){Ext4Reader(Source(a),strict=true).inode(2)}
 }
}
