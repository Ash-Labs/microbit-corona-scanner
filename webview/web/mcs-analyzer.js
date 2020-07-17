/* yes this BLE parsing code is a mess
but there's already tons of BLE scanner smartphone apps out there
so I'm only trying to parse the bare minimum of the data */

function hex2array(hex) {
  let res = [];
  for (let i = 0, j = 0; i < hex.length; i += 2, j++)
	  res[j] = parseInt(hex.substr(i, 2), 16);
  return res;
}

function tohex(v) {
  return (v < 16 ? '0' : '') + (v).toString(16);
}

function array2hex(a) {
  let res = '';
  for (let i = 0; i < a.length; i++)
	  res += tohex(a[i]);
  return res;
}

function encodeHTML(s) {
  return s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/"/g, '&quot;');
}

function searchable(s) {
  let escaped = encodeHTML(s);
  let uri_enc = encodeURI(s);
  let ddgo = '<a href=https://duckduckgo.com/?q=' + uri_enc + ' target="_blank">DuckDuckGo</a>';
  let google = '<a href=https://www.google.com/search?q=' + uri_enc + ' target="_blank">Google</a>';
  return escaped + '&nbsp;' + ddgo + '&nbsp;' + google;
}

function localname(a) {
  let res = "";
  for (let i = 0; i < a.length; i++)
	  res += String.fromCharCode(a[i]);
  return searchable(res);
}

function u16_conv(a, idx) {
  return tohex(a[idx + 1]) + tohex(a[idx]);
}

function lookup_id(a, id) {
  let id_int = parseInt(id, 16);
  return (a[id_int] != null) ? a[id_int] : 'unknown';
}

function appearance(a) {
  let val = u16_conv(a, 0);
  return '<table><tr><td>' + val + '</td><td>' + lookup_id(ble_appearance, val) + '</td></tr></table>';
}

function uuid16_list(a) {
  let res = "<table>";
  for (let i = 0; i < a.length; i += 2) {
	  let uuid = u16_conv(a, i);
	  let uuid_desc = lookup_id(uuid16_types, uuid);
	  if (uuid_desc == 'unknown') uuid_desc = lookup_id(vendor_uuids, uuid);
	  res += '<tr class="strpd_tr"><td>' + uuid + '</td><td>' + uuid_desc + '</td></tr>';
  }
  return res + '</table>';
}

const uuid128_patt = /^([0-9a-f]{8})([0-9a-f]{4})([0-9a-f]{4})([0-9a-f]{4})([0-9a-f]{12})/;

function uuid128_list(a) {
  let res = "<table>";
  for (let i = 0; i < a.length; i += 16) {
	  let uuid = array2hex(a.slice(i, i + 16).reverse());
	  uuid = uuid.replace(uuid128_patt, "$1-$2-$3-$4-$5")
	  res += '<tr class="strpd_tr"><td>' + searchable(uuid) + '</td></tr>';
  }
  return res + '</table>';
}

function uuid16_sd(a) {
  let res = "<table>";
  let uuid = u16_conv(a, 0);
  let uuid_desc = lookup_id(uuid16_types, uuid);
  if (uuid_desc == 'unknown') uuid_desc = lookup_id(vendor_uuids, uuid);
  res += '<tr class="strpd_trb"><td>' + uuid + '</td><td>' + uuid_desc + '</td></tr>';
  res += '<tr class="strpd_trb"><td></td><td>' + array2hex(a.slice(2)) + '</td></tr>';
  return res + '</table>';
}

function manuf_specific(a) {
  let res = "<table>";
  let manuf = u16_conv(a, 0);
  res += '<tr class="strpd_trb"><td>' + manuf + '</td><td>' + lookup_id(company_ids, manuf) + '</td></tr>';
  res += '<tr class="strpd_trb"><td></td><td>' + array2hex(a.slice(2)) + '</td></tr>';
  return res + '</table>';
}

const adv_parser = {
  "type_names": adv_types,

  0x02: uuid16_list,
  0x03: uuid16_list,

  0x06: uuid128_list,
  0x07: uuid128_list,

  0x09: localname,
  0x16: uuid16_sd,
  0x19: appearance,
  0xff: manuf_specific,
};

function parse_data(d, parser) {
  let res = '<table>';
  for (let i = 0; i < d.length;) {
	  let len = d[i];
	  let type = d[i + 1];
	  let data = d.slice(i + 2, i + 2 + len - 1);
	  let typename = parser.type_names[type];
	  typename = (typename != null) ? typename : type;
	  let value_parser = parser[type];
	  let parsed_data;
	  res += '<tr class="strpd_trb"><td>' + typename + '</td></tr>';
	  if (value_parser == null)
		  parsed_data = array2hex(data);
	  else if (typeof(value_parser) == 'function')
		  parsed_data = value_parser(data);
	  else
		  parsed_data = parse_data(data, value_parser);
	  res += '<tr class="strpd_trb"><td>' + parsed_data + '</td></tr>';
	  i += len + 1;
  }
  return res + '</table></br>';
}

function tbl_tostring(value, index, array) {
  return index + ' ' + value;
}

let led_to_bdaddr = {};
let bdaddr_list = {};

function update_advinfo(changed) {
  let ext_info = document.getElementById('adv_info');
  let bdaddr_sp = document.getElementById('bdaddr_td');
  let rssi_sp = document.getElementById('rssi_td');

  let bdaddr = led_to_bdaddr[selected_led];
  if (bdaddr == null) {
	  ext_info.textContent = '';
	  bdaddr_sp.textContent = '';
	  rssi_sp.textContent = '';
	  return;
  }

  let tbl = bdaddr_list[bdaddr];
  let advertisements = tbl.advertisements;
  let adv_list = Object.keys(advertisements);

  if ((changed == null) || changed.bdaddr)
	  bdaddr_sp.textContent = array2hex(hex2array(bdaddr).reverse()); /* correct bdaddr byte order */
  if ((changed == null) || changed.rssi)
	  rssi_sp.textContent = tbl.rssi;

  if ((changed != null) && (!changed.adv) && (!changed.bdaddr))
	  return;

  let text = '';
  for (let i = 0; i < adv_list.length; i++) {
	  //text += adv_list[i] + '\r\n';
	  text += parse_data(hex2array(adv_list[i]), adv_parser);
  }
  ext_info.innerHTML = text;
}

function rgb(r, g, b) {
  r = Math.floor(r);
  g = Math.floor(g);
  b = Math.floor(b);
  return "#" + tohex(r) + tohex(g) + tohex(b);
}

const min_rssi = -98;
const max_rssi = -56;

const refresh_hz = 50;
const refresh_delay = (1000 / refresh_hz);
const led_maxage = refresh_hz * 2;

let led_age = [];
let led_rssi = [];

function update_led(led_idx) {
  let rssi = led_rssi[led_idx];
  let age = led_age[led_idx];

  if (rssi == null)
	  return;

  let led = document.getElementById('led_' + led_idx);
  if (led.className != 'led_td')
	  led.className = 'led_td';

  /* normalize to 0 .. 1.0 */
  rssi -= min_rssi;
  rssi /= max_rssi - min_rssi;

  let age_factor = (led_maxage - age) / led_maxage;

  rssi *= age_factor;

  let min_red = 0x20;
  let red = min_red + (0xff - min_red) * rssi;
  let gb = 0x40 * rssi;
  let hexcol = rgb(red, gb, gb);

  led.bgColor = hexcol;
}

let selected_led = -1;

function adv_rx(adv_data) {
  let led_idx = parseInt(adv_data[1], 16);
  let bdaddr = adv_data[2];
  let data = adv_data[3];
  let changed = {
	  rssi: false,
	  bdaddr: false,
	  adv_date: false,
	  adv: false
  };

  let rssi = parseInt(adv_data[4], 10);
  if (led_idx < 25) {
	  led_rssi[led_idx] = Math.max(Math.min(rssi, max_rssi), min_rssi);
	  led_age[led_idx] = 0;
	  update_led(led_idx);
  }

  changed.bdaddr = (led_to_bdaddr[led_idx] != bdaddr);
  if (changed.bdaddr)
	  led_to_bdaddr[led_idx] = bdaddr;

  if (bdaddr_list[bdaddr] == null) {
	  bdaddr_list[bdaddr] = {
		  "advertisements": {},
	  }
  }
  let t = bdaddr_list[bdaddr];

  changed.rssi = (t.rssi != rssi);
  t.rssi = rssi;
  let adv = t.advertisements;
  changed.adv = (adv[data] == null);
  adv[data] = {
	  "rssi": rssi,
	  "date": new Date()
  };
  changed.adv_date = true; /* date always changes */
  if (led_idx == selected_led)
	  update_advinfo(changed);
}

const adv_patt = /^([0-9a-f]{2}) ([0-9a-f]{12}) ([0-9a-f]+) (-\d\d)/;

let line_idx = 0;
let line_lengths = [];

function ws_rx(evt) {
  let received_msg = evt.data;
  let adv_data = adv_patt.exec(received_msg)

  /* not raw advertisement data */
  if (adv_data == null) {
	  if (/seen:/.test(received_msg)) {
		  let elem = document.getElementById('info');
		  elem.textContent = received_msg;
	  }
	  return;
  }

  adv_rx(adv_data);

  /* show in textarea */
  received_msg += '\r\n';
  let textarea = document.getElementById('message');
  let rows = textarea.rows;
  let text = textarea.textContent;
  let old_len = line_lengths[line_idx];
  text = (line_lengths[0] == 0) ? '' : text.substr(old_len);
  line_lengths[line_idx++] = received_msg.length;
  line_idx %= rows;
  text += received_msg;
  textarea.textContent = text;
}

function led_getidx(e) {
  return (e == null) ? -1 : Number(e.id.substr(4, 2));
}

let led_clicked = false;

function select_led(e, click) {
  /* only a new click can override a clicked LED */
  if (led_clicked && (!click))
	  return;

  let last_clicked = led_clicked;
  let last = selected_led;
  let new_idx = led_getidx(e);

  /* ignore unchanged idx unless we got a click (-> deselect then) */
  if ((last == new_idx) && (!click))
	  return;

  /* deselect old LED */
  if (selected_led >= 0) {
	  let elem = document.getElementById('led_' + selected_led);
	  elem.innerHTML = '';
	  led_clicked = false;
	  selected_led = -1;
	  document.getElementById('adv_td').hidden = true;
  }

  /* new led selected */
  if ((new_idx >= 0) && ((!last_clicked) || (new_idx != last))) {
	  selected_led = new_idx;
	  led_clicked = click;
	  document.getElementById('adv_td').hidden = false;
	  if (click)
		  e.innerHTML = '<center><b>⭘</b></center>';
  }

  update_advinfo();
}

function led_timer() {
  for (let i = 0; i < 25; i++) {
	  if ((led_age[i] != null) && (led_age[i] < led_maxage)) {
		  led_age[i]++;
		  update_led(i);
	  }
  }
}

function populate_table() {
  let tbl = document.getElementById('leds');
  let html = "";
  let idx = 0;
  for (let y = 0; y < 5; y++) {
	  html += '<tr>';
	  for (let x = 0; x < 5; x++, idx++) {
		  html += '<td id="led_' + idx + '" class="led_td_unused" onmouseover="select_led(this, false)" onmouseout="select_led(null, false)" onclick="select_led(this, true)"></td>';
	  }
	  html += '</tr>';
  }
  tbl.innerHTML = html;
}

function ws_closed() {
  let textarea = document.getElementById('message');
  textarea.textContent = 'No connection to WebSocket!\r\nReload page to try again.';
}

document.addEventListener("DOMContentLoaded", function() {
  populate_table();
  for (let i = 0; i < document.getElementById('message').rows; i++)
	  line_lengths[i] = 0;
  let ws = new WebSocket("ws://localhost:8090/");
  ws.onclose = ws_closed;
  ws.onmessage = ws_rx;
  window.setInterval(led_timer, refresh_delay);
});
