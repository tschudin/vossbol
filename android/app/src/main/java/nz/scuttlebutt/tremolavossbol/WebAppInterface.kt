package nz.scuttlebutt.tremolavossbol

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Intent
import android.content.pm.PackageManager
import android.util.Base64
import android.util.Log
import android.webkit.JavascriptInterface
import android.webkit.WebView
import android.widget.Toast
import androidx.core.content.ContextCompat.checkSelfPermission
import com.google.zxing.integration.android.IntentIntegrator
import nz.scuttlebutt.tremolavossbol.tssb.LogTinyEntry
import nz.scuttlebutt.tremolavossbol.utils.Bipf
import nz.scuttlebutt.tremolavossbol.utils.Bipf.Companion.BIPF_LIST
import nz.scuttlebutt.tremolavossbol.utils.Bipf.Companion.bipf_list2JSON
import nz.scuttlebutt.tremolavossbol.utils.Bipf.Companion.decode
import nz.scuttlebutt.tremolavossbol.utils.Bipf.Companion.mkDict
import nz.scuttlebutt.tremolavossbol.utils.Bipf.Companion.mkList
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_APP_BODY
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_APP_BOX
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_APP_KANBAN
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_APP_RECP
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_APP_TEXTANDMEDIA
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_ATTACH_AUDIO_CODEC2
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_ATTACH_TIME
import nz.scuttlebutt.tremolavossbol.utils.Constants.Companion.TINYSSB_ATTACH_UTF8_TEXT
import nz.scuttlebutt.tremolavossbol.utils.HelperFunctions.Companion.deRef
import nz.scuttlebutt.tremolavossbol.utils.HelperFunctions.Companion.toBase64
import nz.scuttlebutt.tremolavossbol.utils.HelperFunctions.Companion.toHex
import org.json.JSONArray
import org.json.JSONObject


// pt 3 in https://betterprogramming.pub/5-android-webview-secrets-you-probably-didnt-know-b23f8a8b5a0c

class WebAppInterface(val act: MainActivity, val webView: WebView) {

    var frontend_ready = false

    @JavascriptInterface
    fun onFrontendRequest(s: String) {
        //handle the data captured from webview}
        Log.d("FrontendRequest", s)
        val args = s.split(" ")
        when (args[0]) {
            "onBackPressed" -> { // When 'back' is pressed, will close app
                (act as MainActivity)._onBackPressed()
            }

            "ready" -> { // Initialisation, send localID to frontend
                eval("b2f_initialize(\"${act.idStore.identity.toRef()}\")")
                frontend_ready = true
                act.tinyNode.beacon()
            }

            "reset" -> { // UI reset
                // erase DB content
                eval("b2f_initialize(\"${act.idStore.identity.toRef()}\")")
            }

            "restream" -> { // Resend all the logs
                for (fid in act.tinyRepo.listFeeds()) {
                    Log.d("wai", "restreaming ${fid.toHex()}")
                    var i = 1
                    while (true) {
                        val (payload, mid) = act.tinyRepo.feed_read_content(fid, i)
                        if (payload == null) break
                        Log.d("restream", "${i}, ${payload.size} Bytes")
                        sendToFrontend(fid, i, mid!!, payload)
                        i++
                    }
                }
            }

            "qrscan.init" -> { // start scanning the qr code (open the camera)
                val intentIntegrator = IntentIntegrator(act)
                intentIntegrator.setBeepEnabled(false)
                intentIntegrator.setCameraId(0)
                intentIntegrator.setPrompt("SCAN")
                intentIntegrator.setBarcodeImageEnabled(false)
                intentIntegrator.initiateScan()
                return
            }

            "secret:" -> { // import a new ID (is not used)
                if (importIdentity(args[1])) {
                    /*
                    tremolaState.logDAO.wipe()
                    tremolaState.contactDAO.wipe()
                    tremolaState.pubDAO.wipe()
                    */
                    act.finishAffinity()
                }
                return
            }

            "exportSecret" -> { // Show the secret key (both as string and qr code)
                val json = act.idStore.identity.toExportString()!!
                eval("b2f_showSecret('${json}');")
                val clipboard = act.getSystemService(ClipboardManager::class.java)
                val clip = ClipData.newPlainText("simple text", json)
                clipboard.setPrimaryClip(clip)
                Toast.makeText(act, "secret key was also\ncopied to clipboard",
                    Toast.LENGTH_LONG).show()
            }

            "wipe" -> { // Delete all data about the peer, included ID (not revertible)
                act.settings!!.resetToDefault()
                act.idStore.setNewIdentity(null) // creates new identity
                act.tinyRepo.repo_reset()
                // eval("b2f_initialize(\"${tremolaState.idStore.identity.toRef()}\")")
                // FIX ME: should kill all active connections, or better then the app
                act.finishAffinity()
            }

            "add:contact" -> { // Add a new contact
                // Only store in database and advertise it to connected peers via SSB event.
                val id = args[1].substring(1, args[1].length - 8)
                Log.d("ADD", id)
                act.tinyGoset._add_key(Base64.decode(id, Base64.NO_WRAP))
            }
            /* no alias publishing in tinyTremola
            "add:contact" -> { // ID and alias
                tremolaState.addContact(args[1],
                    Base64.decode(args[2], Base64.NO_WRAP).decodeToString())
                val rawStr = tremolaState.msgTypes.mkFollow(args[1])
                val evnt = tremolaState.msgTypes.jsonToLogEntry(rawStr,
                    rawStr.encodeToByteArray())
                evnt?.let {
                    rx_event(it) // persist it, propagate horizontally and also up
                    tremolaState.peers.newContact(args[1]) // inform online peers via EBT
                }
                    return
            }
            */

            "post" -> { // post tips atob(text) atob(voice) rcp1 rcp2 ...
                // for public chats, rcp1 is "null"
                val a = JSONArray(args[1])
                val tips = ArrayList<String>(0)  // for voice
                for (i in 0..a.length() - 1) {
                    val post = (a[i] as JSONObject).toString()
                    Log.d("post", post)
                    tips.add(post)
                }
                var t: String? = null
                if (args[2] != "null")
                    t = Base64.decode(args[2], Base64.NO_WRAP).decodeToString()
                var v: ByteArray? = null
                if (args.size > 3 && args[3] != "null")
                    v = Base64.decode(args[3], Base64.NO_WRAP)
                Log.d( "post", "tips = $tips, text = $t, voice = $v, rcps = ${args.slice(4..args.lastIndex)}")
                post_with_voice(tips, t, v, args.slice(4..args.lastIndex))
                return
            }

            "get:media" -> {
                if (checkSelfPermission(act, Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                    Toast.makeText(act, "No permission to access media files",
                        Toast.LENGTH_SHORT).show()
                    return
                }
                val intent = Intent(Intent.ACTION_OPEN_DOCUMENT); // , MediaStore.Images.Media.EXTERNAL_CONTENT_URI)
                intent.type = "image/*"
                act.startActivityForResult(intent, 1001)
            }

            "get:voice" -> { // get:voice
                Log.e("get:voice", "Trying to record audio")
                return
//                val intent = Intent(act, RecordActivity::class.java)
//                act.startActivityForResult(intent, 808)
//                return
            }

            "play:voice" -> { // play:voice b64enc(codec2) from date)
                Log.e("get:voice", "Trying to play audio")
                return
            }

            "kanban" -> { // kanban bid atob(prev) atob(operation) atob(arg1) atob(arg2) atob(...)
                /*var bid: String = args[1]
                var prevs: List<String>? = null
                if(args[2] != "null") // prevs == "null" for the first board event (create bord event)
                    prevs = Base64.decode(args[2], Base64.NO_WRAP).decodeToString().split(" ")
                var operation: String = Base64.decode(args[3], Base64.NO_WRAP).decodeToString()
                var argList: List<String>? = null
                if(args[4] != "null")
                    argList = Base64.decode(args[4], Base64.NO_WRAP).decodeToString().split(" ")

                 */
                //var data = JSONObject(Base64.decode(args[1], Base64.NO_WRAP).decodeToString())
                val bid: String? = if (args[1] != "null") args[1] else null
                val prev: List<String>? = if (args[2] != "null") Base64.decode(args[2], Base64.NO_WRAP)
                    .decodeToString().split(",").map { Base64.decode(it, Base64.NO_WRAP).decodeToString() } else null
                val op: String = args[3]
                val argsList: List<String>? = if (args[4] != "null") Base64.decode(args[4], Base64.NO_WRAP)
                    .decodeToString().split(",").map { Base64.decode(it, Base64.NO_WRAP).decodeToString() } else null

                if (bid != null) {
                    Log.d("KanbanPostBID", bid)
                    Log.d("KanbanPostPREV", prev.toString())
                }
                Log.d("KanbanPostOP", op)
                Log.d("KanbanPostARGS", args.toString())

                kanban(bid, prev, op, argsList)
            }

            "settings:set" -> {
                when(args[1]) {
                    "ble" -> {act.settings!!.setBleEnabled(args[2].toBooleanStrict())}
                    "udp_multicast" -> {act.settings!!.setUdpMulticastEnabled(args[2].toBooleanStrict())}
                    "websocket" -> {act.settings!!.setWebsocketEnabled(args[2].toBooleanStrict())}
                }
            }

            else -> {
                Log.d("onFrontendRequest", "unknown")
            }
        }
    }

    fun eval(js: String) { // send JS string to webkit frontend for execution
        webView.post(Runnable {
            webView.evaluateJavascript(js, null)
        })
    }

    /**
     * Only called (but commented out) from tremola.js::menu_import_id,
     * which is never called (Menu item leading to it is commented out)
     */
    private fun importIdentity(secret: String): Boolean {
        Log.d("D/importIdentity", secret)
        if (act.idStore.setNewIdentity(Base64.decode(secret, Base64.DEFAULT))) {
            // FIX ME: remove all decrypted content in the database, try to decode new one
            Toast.makeText(act, "Imported of ID worked. You must restart the app.",
                Toast.LENGTH_SHORT).show()
            return true
        }
        Toast.makeText(act, "Import of new ID failed.", Toast.LENGTH_LONG).show()
        return false
    }

    fun post_with_voice(tips: ArrayList<String>, text: String?, voice: ByteArray?, rcps: List<String>?) {
        if (text != null)
            Log.d("wai", "post_text t- ${text}/${text.length}")
        if (voice != null)
            Log.d("wai", "post_voice v- ${voice}/${voice.size}")

        //  Prepare attachments
        val body = mkDict()
        if (text != null) {
            Bipf.dict_append(body, TINYSSB_ATTACH_UTF8_TEXT, Bipf.mkString(text))
        }
        if (voice != null) {
            Bipf.dict_append(body, TINYSSB_ATTACH_AUDIO_CODEC2, Bipf.mkBytes(voice))
        }

        // Prepare post
        val post = mkDict()
        Bipf.dict_append(post, TINYSSB_APP_BODY, body)

        val tst = Bipf.mkInt((System.currentTimeMillis() / 1000).toInt())
        Bipf.dict_append(post, TINYSSB_ATTACH_TIME, tst)
        Log.d("wai", "send time is ${tst.getInt()}")

        // Prepare message
        val packet = mkList()
        if (rcps!!.isNotEmpty()) {  // private message: add recipients list and encrypt
            // Prepare the list of recipients ("recps" as a field in the post and "keys" for the encryption)
            val recps = mkList()
            val keys: MutableList<ByteArray> = mutableListOf()
            val me = act.idStore.identity.toRef()
            for (r in rcps) {
                if (!r.deRef().contentEquals(me.deRef())) {
                    Bipf.list_append(recps, Bipf.mkBytes(r.deRef()))
                    keys.add(r.deRef())
                }
            }
            Bipf.list_append(recps, Bipf.mkBytes(me.deRef()))
            keys.add(me.deRef())
            Bipf.dict_append(post, TINYSSB_APP_RECP, recps)

            val msg = mkList()
            Bipf.list_append(msg, TINYSSB_APP_TEXTANDMEDIA)
            Bipf.list_append(msg, post)

            val encrypted = act.idStore.identity.encryptPrivateMessage(Bipf.encode(msg)!!, keys)
            Bipf.list_append(packet, TINYSSB_APP_BOX)
            Bipf.list_append(packet, Bipf.mkBytes(encrypted))
            Log.d("wai", "Sending encrypted bipf: ${bipf_list2JSON(msg)}")
        } else { // public message
            Bipf.list_append(packet, TINYSSB_APP_TEXTANDMEDIA)
            Bipf.list_append(packet, post)
        }

        Log.d("wai", "Sending bipf: ${bipf_list2JSON(packet)}")
        act.tinyNode.publish_content(Bipf.encode(packet)!!)
    }

    fun kanban(bid: String?, prev: List<String>?, operation: String, args: List<String>?) {
        val lst = mkList()
        Bipf.list_append(lst, TINYSSB_APP_KANBAN)
        if (bid != null)
            Bipf.list_append(lst, Bipf.mkString(bid))
        else
            Bipf.list_append(lst, Bipf.mkString("null")) // Bipf.mkNone()

        if (prev != null) {
            val prevList = mkList()
            for (p in prev) {
                Bipf.list_append(prevList, Bipf.mkString(p))
            }
            Bipf.list_append(lst, prevList)
        } else {
            Bipf.list_append(lst, Bipf.mkString("null")) // Bipf.mkNone()
        }

        Bipf.list_append(lst, Bipf.mkString(operation))

        if (args != null) {
            for (arg in args) {
                Bipf.list_append(lst, Bipf.mkString(arg))
            }
        }

        val body = Bipf.encode(lst)

        if (body != null) {
            Log.d("kanban", "published bytes: " + decode(body))
            act.tinyNode.publish_content(body)
        }
        //val body = Bipf.encode(lst)
        //Log.d("KANBAN BIPF ENCODE", Bipf.bipf_list2JSON(Bipf.decode(body!!)!!).toString())
        //if (body != null)
        //act.tinyNode.publish_public_content(body)

    }

    fun return_voice(voice: ByteArray) {
        var cmd = "b2f_new_voice('" + voice.toBase64() + "');"
        Log.d("CMD", cmd)
        eval(cmd)
    }

    fun sendTinyEventToFrontend(entry: LogTinyEntry) {
        sendToFrontend(entry.fid, entry.seq, entry.mid, entry.body)
    }

    fun sendToFrontend(fid: ByteArray, seq: Int, mid: ByteArray, payload: ByteArray) {
        Log.d("send", "sendToFrontend seq=${seq} ${payload.toHex()}")
        var confid = false
        var bodyList = decode(payload)
        if (bodyList == null || bodyList.typ != BIPF_LIST) {
            Log.d("send", "decoded payload == null")
            return
        }
        var body = bipf_list2JSON(bodyList)
//        Log.d("send", "box = $body")
        if (body!![0] == TINYSSB_APP_BOX.getString()) { //private, decrypt
//            Log.d("sendToFrontend", body.toString())
            val x = act.idStore.identity.decryptPrivateMessageString(body[1] as String)
            bodyList = decode(x!!)
            if (bodyList == null || bodyList.typ != BIPF_LIST) {
                Log.d("send", "decrypted payload == null")
                return
            }
            body = bipf_list2JSON(bodyList)
            confid = true
        }
        Log.d("send", "sending $body")

        if (body!![0] == TINYSSB_APP_TEXTANDMEDIA.getString()) { // Text and media, send message

            val param = JSONObject()
            param.put("TAM", body[1])
            Log.d("send", "param = $param")
            val hdr = JSONObject()
            hdr.put("fid", "@" + fid.toBase64() + ".ed25519")
            hdr.put("ref", mid.toBase64())
            hdr.put("seq", seq)
            var cmd = "b2f_new_event({\"header\":${hdr},"
            if (confid) {
                cmd += "\"public\":${null},"
                cmd += "\"confid\":${param}"
            } else {
                cmd += "\"public\":${param},"
                cmd += "\"confid\":${null}"
            }
            cmd += "});"
            Log.d("CMD", "send : $cmd")
            eval(cmd)
        } else if (body[0] == TINYSSB_APP_KANBAN.getString()) { //private, decrypt
            val param = bipf_list2JSON(bodyList)
            val hdr = JSONObject()
            hdr.put("fid", "@" + fid.toBase64() + ".ed25519")
            hdr.put("ref", mid.toBase64())
            hdr.put("seq", seq)
            var cmd = "b2f_new_event({\"header\":${hdr},"
            cmd += "\"public\":${param.toString()}"
            cmd += "});"
            Log.d("CMD", cmd)
            eval(cmd)
        } else {
            Log.d("sendToFrontend", "Packet format ${body[0]} not recognised")
        }
    }
}


/**
 * Ideal message layout:
 *
 * ['TAM', {'RCP': RCPS*, 'XRF', XREF*, 'TIM', time, 'BDY': {'TXU8': time, 'IMPG', png_image, 'LOGP', gps_coordinates}}]
 */