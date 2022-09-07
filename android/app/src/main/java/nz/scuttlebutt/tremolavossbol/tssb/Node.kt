package nz.scuttlebutt.tremolavossbol.tssb

import android.content.Context
import android.util.Log
import nz.scuttlebutt.tremolavossbol.MainActivity
import nz.scuttlebutt.tremolavossbol.utils.Bipf
import nz.scuttlebutt.tremolavossbol.utils.Bipf.Companion.BIPF_LIST
import nz.scuttlebutt.tremolavossbol.utils.Bipf.Companion.bipf_loads
import nz.scuttlebutt.tremolavossbol.utils.Bipf.Companion.varint_decode
import nz.scuttlebutt.tremolavossbol.utils.Constants
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.DMX_LEN
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.HASH_LEN
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.PKTTYPE_chain20
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_PKT_LEN
import nz.scuttlebutt.tremolavossbol.utils.HelperFunctions
import nz.scuttlebutt.tremolavossbol.utils.HelperFunctions.Companion.decodeHex
import nz.scuttlebutt.tremolavossbol.utils.HelperFunctions.Companion.toByteArray
import nz.scuttlebutt.tremolavossbol.utils.HelperFunctions.Companion.toHex
import java.io.File
import java.io.RandomAccessFile
import java.util.concurrent.locks.ReentrantLock

class Node(val context: MainActivity) {
    val NODE_ROUND_LEN = 5000L
    var rot = 0 // rotate the want and chunk lists

    fun incoming_want_request(buf: ByteArray, aux: ByteArray?) {
        Log.d("node", "incoming WANT ${buf.toHex()}")
        val vect = bipf_loads(buf.sliceArray(DMX_LEN..buf.lastIndex))
        if (vect == null || vect.typ != BIPF_LIST) return
        var v = "WANT vector=["
        var credit = 3
        for (e in vect.getList()) {
            val fid: ByteArray
            var seq: Int
            try {
                val lst = e.getList()
                fid = context.tinyGoset.keys[lst[0].getInt()]
                seq = lst[1].getInt()
                v += " ${fid.sliceArray(0..9).toHex()}.${seq}"
            } catch (e: Exception) {
                Log.d("node", "incoming WANT error ${e.toString()}")
                continue
            }
            // Log.d("node", "want ${fid.toHex()}.${seq}")
            while (credit > 0) {
                val pkt = context.tinyRepo.feed_read_pkt(fid, seq)
                if (pkt == null) {
                    // Log.d("node", "  no record at ${seq}")
                    break
                }
                Log.d("node", "  have entry ${fid.toHex()}.${seq}")
                context.tinyIO.enqueue(pkt)
                seq++;
                credit--;
            }
        }
        v += " ]"
        Log.d("node", v)
        if (credit == 3)
            Log.d("node", "  no entry found to serve")
    }

    fun incoming_chunk_request(buf: ByteArray, aux: ByteArray?) {
        Log.d("node", "incoming CHNK request")
        val vect = bipf_loads(buf.sliceArray(DMX_LEN..buf.lastIndex))
        if (vect == null || vect.typ != BIPF_LIST) {
            Log.d("node", "  malformed?")
            return
        }
        var v= "CHNK vector=["
        var credit = 3
        for (e in vect.getList()) {
            val fNDX: Int
            val fid: ByteArray
            val seq: Int
            var cnr: Int
            try {
                val lst = e.getList()
                fNDX = lst[0].getInt()
                fid = context.tinyGoset.keys[fNDX]
                seq = lst[1].getInt()
                cnr = lst[2].getInt()
                v += " ${fid.sliceArray(0..9).toHex()}.${seq}.${cnr}"
            } catch (e: Exception) {
                Log.d("node", "incoming CHNK error ${e.toString()}")
                continue
            }
            val pkt = context.tinyRepo.feed_read_pkt(fid, seq)
            if (pkt == null || pkt[DMX_LEN].toInt() != PKTTYPE_chain20) continue;
            val (sz, szlen) = varint_decode(pkt, DMX_LEN + 1, DMX_LEN + 4)
            if (sz <= 48 - szlen) continue;
            val maxChunks    = (sz - (48 - szlen) + 99) / 100
            Log.d("node", "maxChunks is ${maxChunks}")
            if (cnr > maxChunks) continue
            while (cnr <= maxChunks && credit-- > 0) {
                val chunk = context.tinyRepo.feed_read_chunk(fid, seq, cnr)
                if (chunk == null) break;
                Log.d("node", "  have chunk ${fid.sliceArray(0..19).toHex()}.${seq}.${cnr}")
                context.tinyIO.enqueue(chunk);
                cnr++;
            }
        }
        v += " ]"
        Log.d("node", v)
    }

    fun loop(lck: ReentrantLock) {
        while (true) {
            lck.lock()
            beacon()
            lck.unlock()
            Thread.sleep(NODE_ROUND_LEN)
        }
    }

    fun beacon() { // called in regular intervals
        /*
        Serial.print("|dmxt|=" + String(dmxt_cnt) + ", |chkt|=" + String(blbt_cnt));
        int fcnt, ecnt, bcnt;
        stats(&fcnt, &ecnt, &bcnt);
        */
        Log.d("node", "beacon") // , stats: |feeds|=" + String(fcnt) + ", |entries|=" + String(ecnt) + ", |blobs|=" + String(bcnt));

        // FIXME: limit vector to 100B, rotate through set
        val vect = Bipf.mkList()
        var i = 0
        for (k in context.tinyGoset.keys) {
            val feed = context.tinyRepo.feeds[context.tinyRepo._feed_index(k)]
            val e = Bipf.mkList()
            Bipf.list_append(e, Bipf.mkInt(i))
            Bipf.list_append(e, Bipf.mkInt(feed.next_seq))
            Bipf.list_append(vect, e)
            i++
            // Log.d("node", "dmx for requested pkt: ${k.toHex()} ${feed.next_seq} ${feed.prev_hash.toHex()}")
            val dmx = context.tinyDemux.compute_dmx(k + feed.next_seq.toByteArray()
                                                           + feed.prev_hash)
            // Log.d("node", "dmx is ${dmx.toHex()}")
            val fct = { buf: ByteArray, fid:ByteArray? -> context.tinyNode.incoming_pkt(buf, fid!!) }
            context.tinyDemux.arm_dmx(dmx, fct, k)
        }
        val buf = Bipf.encode(vect) // vect always has at least one element
        if (buf != null)
            context.tinyIO.enqueue(buf, context.tinyDemux.want_dmx)

        // hunt for unfinished sidechains
        val chunkReqList = Bipf.mkList()
        val fdir = File(context.getDir(context.tinyRepo.TINYSSB_DIR, Context.MODE_PRIVATE), context.tinyRepo.FEED_DIR)
        val r = context.tinyRepo
        for (f in fdir.listFiles()) {
            if (!f.isDirectory || f.name.length != 2* Constants.FID_LEN) continue
            val fid = f.name.decodeHex()
            val frec = context.tinyRepo.fid2rec(fid, true)
            frec!!.next_seq = r.feed_len(fid) + 1
            for (fn in f.listFiles()) {
                if (fn.name[0] != '!')
                    continue
                var seq = fn.name.substring(1..fn.name.lastIndex).toInt()
                val sz = fn.length().toInt()
                var h = ByteArray(HASH_LEN)
                Log.d("node","need chunk ${fid.toHex().substring(0..19)}.${seq}")
                if (sz == 0) { // must fetch first ptr from log
                    val pkt = r.feed_read_pkt(fid, seq)
                    if (pkt != null) {
                        h = pkt.sliceArray(DMX_LEN + 1 + 28..DMX_LEN + 1 + 28 + HASH_LEN - 1)
                        Log.d("node","  having hash ${h.toHex()}")
                    } else {
                        Log.d("node", "  failed to find hash")
                        seq = -1
                    }

                } else { // must fetch latest ptr from chain file
                    Log.d("node", "fetching chunk hash from file ${fn}")
                    val g = RandomAccessFile(fn, "rw")
                    g.seek(g.length() - HASH_LEN)
                    if (g.read(h) != h.size) {
                        Log.d("node", "could not read() after seek")
                        seq = -1;
                    } else {
                        var i = 0
                        while (i < HASH_LEN)
                            if (h[i].toInt() != 0)
                                break;
                            else
                                i++
                        if (i == HASH_LEN) // reached end of chain
                            seq = -1;
                    }
                }
                if (seq > 0) {
                    val nextChunk = sz / TINYSSB_PKT_LEN;
                    // FIXME: check if sidechain is already full, then swap '.' for '!' (e.g. after a crash)
                    val lst = Bipf.mkList()
                    val fidNr = context.tinyGoset.keys.indexOfFirst({
                            k -> HelperFunctions.byteArrayCmp(k, fid) == 0
                    })
                    Bipf.list_append(lst, Bipf.mkInt(fidNr))
                    Bipf.list_append(lst, Bipf.mkInt(seq))
                    Bipf.list_append(lst, Bipf.mkInt(nextChunk))
                    Bipf.list_append(chunkReqList, lst)
                    val fct = { pkt: ByteArray,x: Int -> context.tinyNode.incoming_chunk(pkt,x) }
                    Log.d("node", "need chunk $fidNr.$seq.$nextChunk, armed for ${h.toHex()}, list now ${chunkReqList.cnt} (${lst.cnt})")
                    context.tinyDemux.arm_blb(h, fct, fid, seq, nextChunk)
                }
            }

            {
            }
        }
        if (chunkReqList.cnt > 0) {
            Log.d("node","send CHNK request for ${chunkReqList.cnt} chunks")
            val buf = Bipf.encode(chunkReqList)
            if (buf != null)
                context.tinyIO.enqueue(buf, context.tinyDemux.chnk_dmx)
        }

        /*

        // hunt for unfinished sidechains
        lptr = bipf_mkList();
        File fdir = MyFS.open(FEED_DIR);
        File f = fdir.openNextFile("r");
        while (f) {
            if (f.isDirectory()) {
                unsigned char *fid = from_hex(strrchr(f.name(), '/')+1, FID_LEN); // from_b64(pos, FID_LEN)
                if (fid != NULL) {
                    File ldir = MyFS.open(f.name());
                    File g = ldir.openNextFile("r");
                    while (g) {
                        // Serial.println(String("looking at ") + g.name());
                        char *pos = strchr(g.name(), '!');
                        if (pos != NULL) {
                            int seq = atoi(pos+1);
                            unsigned char h[HASH_LEN];
                            int sz = g.size();
                            if (sz == 0) { // must fetch first ptr from log
                                unsigned char *pkt = repo_feed_read(fid, seq);
                                if (pkt != NULL)
                                    memcpy(h, pkt+DMX_LEN+1+28, HASH_LEN);
                                else
                                    seq = -1;
                            } else { // must fetch latest ptr from chain file
                                g.seek(sz - HASH_LEN, SeekSet);
                                if (g.read(h, HASH_LEN) != HASH_LEN) {
                                    Serial.println("could not read() after seek");
                                    seq = -1;
                                } else {
                                    int i;
                                    for (i = 0; i < HASH_LEN; i++)
                                    if (h[i] != 0)
                                        break;
                                    if (i == HASH_LEN) // reached end of chain
                                        seq = -1;
                                }
                            }
                            if (seq > 0) {
                                int ndx = feed_index(fid);
                                int next_blob = g.size() / TINYSSB_PKT_LEN;
                                // FIXME: check if sidechain is already full, then swap '.' for '!' (e.g. after a crash)
                                struct bipf_s* slptr = bipf_mkList();
                                bipf_list_append(slptr, bipf_mkInt(ndx));
                                bipf_list_append(slptr, bipf_mkInt(seq));
                                bipf_list_append(slptr, bipf_mkInt(next_blob));
                                bipf_list_append(lptr, slptr);
                                arm_blb(h, incoming_chunk, feeds[ndx].fid, seq, next_blob);
                            }
                        }
                        g.close();
                        g = ldir.openNextFile("r");
                    } // while (g)
                }
            }
            f.close();
            f = fdir.openNextFile("r");
        }
        fdir.close();
        if (lptr->cnt > 0) {
            int sz = bipf_encodingLength(lptr);
            unsigned char buf[sz];
            bipf_encode(buf, lptr);
            io_enqueue(buf, sz, chnk_dmx);
            bipf_free(lptr);
        }
        */
    }

    fun incoming_pkt(buf: ByteArray, fid: ByteArray) {
        Log.d("node", "incoming logEntry ${buf.size}B")
        if (buf.size != TINYSSB_PKT_LEN) return
        context.tinyRepo.feed_append(fid, buf)
    }

    fun incoming_chunk(buf: ByteArray, blbt_ndx: Int) {
        Log.d("node", "incoming chunk ${buf.size}B, index=${blbt_ndx}")
        if (buf.size != TINYSSB_PKT_LEN) return
        context.tinyRepo.sidechain_append(buf, blbt_ndx)
    }

    fun publish_public_content(content: ByteArray): Boolean {
        val repo = context.tinyRepo
        Log.d("node", "publish_public_content ${content.size}B")
        val pkt = repo.mk_contentLogEntry(content)
        Log.d("node", "publish_public_content --> pkt ${if (pkt == null) 0 else pkt.size}B")
        if (pkt == null) return false
        return repo.feed_append(context.idStore.identity.verifyKey, pkt)
    }
}