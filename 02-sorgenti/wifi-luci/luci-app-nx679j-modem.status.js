'use strict';
'require view';
'require rpc';
'require poll';
'require ui';

const callStatus = rpc.declare({ object: 'luci.nx679j-modem', method: 'getStatus', expect: {} });
const callReconnect = rpc.declare({ object: 'luci.nx679j-modem', method: 'doReconnect', expect: { queued: false } });
const callPin = rpc.declare({ object: 'luci.nx679j-modem', method: 'doPin', params: [ 'pin' ], expect: { ok: false } });

function rows(pairs) {
	return pairs.filter(p => p[1] != null && p[1] !== '').map(p => E('tr', { 'class': 'tr' }, [
		E('td', { 'class': 'td left', 'width': '38%' }, p[0]),
		E('td', { 'class': 'td left' }, String(p[1]))
	]));
}

function section(title, pairs) {
	const r = rows(pairs);
	return E('div', { 'class': 'cbi-section' }, [
		E('h3', {}, title),
		r.length ? E('table', { 'class': 'table' }, E('tbody', {}, r)) : E('em', {}, _('Section is empty.'))
	]);
}

return view.extend({
	load: function() {
		return callStatus();
	},

	render: function(data) {
		const s = data || {};
		const view_node = E('div', {}, [
			E('h2', {}, _('Cellular Network')),
			E('p', { 'class': 'cbi-section-descr' }, _('Modem X65 via QMI over QRTR — no ModemManager')),
			section(_('Modem'), [
				[ _('Operator'), s.operator ],
				[ _('Registration'), s.registration ],
				[ _('Access technology'), s.tech ],
				[ _('IMEI'), s.imei ],
				[ _('Firmware revision'), s.revision ]
			]),
			section(_('Signal'), [
				[ 'RSSI', s.rssi ], [ 'RSRP', s.rsrp ], [ 'RSRQ', s.rsrq ], [ 'SNR', s.snr ]
			]),
			section(_('SIM'), [
				[ _('Slot 1'), s.slot1 ], [ _('Slot 2'), s.slot2 ],
				[ 'ICCID', s.iccid ], [ 'IMSI', s.imsi ]
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, _('SIM PIN')),
				E('div', { 'class': 'cbi-value' }, [
					E('label', { 'class': 'cbi-value-title' }, _('Unlock with PIN1')),
					E('div', { 'class': 'cbi-value-field' }, [
						E('input', { 'type': 'password', 'id': 'nx-pin', 'class': 'cbi-input-password' }),
						E('button', {
							'class': 'cbi-button cbi-button-apply',
							'click': ui.createHandlerFn(this, function() {
								const el = document.getElementById('nx-pin');
								if (!el || !el.value) {
									ui.addNotification(null, E('p', {}, _('Enter the PIN first.')));
									return;
								}
								return callPin(el.value).then(r => {
									ui.addNotification(null, E('p', {}, r.ok
										? _('PIN accepted.')
										: _('PIN not accepted: ') + (r.err || '')));
									el.value = '';
								});
							})
						}, _('Unlock'))
					])
				])
			]),
			section(_('Data connection'), [
				[ _('IPv4 address'), s.ip ],
				[ _('Default route'), s.route ? _('present') : _('absent') ],
				[ _('QMI session'), s.session ? _('active') : _('not running') ],
				[ _('System uptime'), s.uptime ? '%t'.format(s.uptime) : null ]
			]),
			E('div', { 'class': 'cbi-page-actions' }, E('button', {
				'class': 'cbi-button cbi-button-action',
				'click': ui.createHandlerFn(this, function() {
					return callReconnect().then(() => {
						ui.addNotification(null, E('p', {}, _('Renewal requested.')));
					});
				})
			}, _('Reconnect')))
		]);

		poll.add(() => callStatus().then(res => {
			// Non ri-renderizzare mentre l'utente sta digitando il PIN: il replace
			// del nodo distruggerebbe il campo e il valore inserito.
			const p = document.getElementById('nx-pin');
			if (p && (p.value || document.activeElement === p))
				return;
			const n = view_node;
			n.parentNode && n.parentNode.replaceChild(this.render(res), n);
		}), 5);

		return view_node;
	}
});
