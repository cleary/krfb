
/***************************************************************************
                                kinetd.cpp
                              --------------
    begin                : Mon Feb 11 2002
    copyright            : (C) 2002 by Tim Jansen
    email                : tim@tjansen.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "kinetd.h"
#include "kinetd.moc"
#include <kservicetype.h>
#include <kdebug.h>
#include <kstandarddirs.h>
#include <kconfig.h>
#include <knotifyclient.h>
#include <ksockaddr.h>
#include <kextsock.h>
#include <klocale.h>

PortListener::PortListener(KService::Ptr s, KConfig *config) :
	m_socket(0),
	m_config(config)
{
	loadConfig(s);

	if (m_enabled)
		acquirePort();
	else
		m_port = -1;
}

void PortListener::acquirePort() {
	m_port = m_portBase;
	m_socket = new KServerSocket(m_port, false);
	while (!m_socket->bindAndListen()) {
		m_port++;
		if (m_port >= (m_portBase+m_autoPortRange)) {
			kdDebug() << "Kinetd cannot load service "<<m_serviceName
				  <<": unable to get port" << endl;
			m_port = -1;
			m_enabled = false;
			delete m_socket;
			m_socket = 0;
			return;
		}
		delete m_socket;
		m_socket = new KServerSocket(m_port, false);
	}
	connect(m_socket, SIGNAL(accepted(KSocket*)),
		SLOT(accepted(KSocket*)));
}

void PortListener::loadConfig(KService::Ptr s) {
	m_valid = true;
	m_autoPortRange = 0;
	m_enabled = true;
	m_argument = QString::null;
	m_multiInstance = false;

	QVariant vid, vport, vautoport, venabled, vargument, vmultiInstance;

	m_execPath = s->exec().utf8();
	vid = s->property("X-KDE-KINETD-id");
	vport = s->property("X-KDE-KINETD-port");
	vautoport = s->property("X-KDE-KINETD-autoPortRange");
	venabled = s->property("X-KDE-KINETD-enabled");
	vargument = s->property("X-KDE-KINETD-argument");
	vmultiInstance = s->property("X-KDE-KINETD-multiInstance");

	if (!vid.isValid()) {
		kdDebug() << "Kinetd cannot load service "<<m_serviceName
			  <<": no id set" << endl;
		m_valid = false;
		return;
	}

	if (!vport.isValid()) {
		kdDebug() << "Kinetd cannot load service "<<m_serviceName
			  <<": invalid port" << endl;
		m_valid = false;
		return;
	}

	m_serviceName = vid.toString();
	m_portBase = vport.toInt();
	if (vautoport.isValid())
		m_autoPortRange = vautoport.toInt();
	if (venabled.isValid())
		m_enabled = venabled.toBool();
	if (vargument.isValid())
		m_argument = vargument.toString();
	if (vmultiInstance.isValid())
		m_multiInstance = vmultiInstance.toBool();

	m_config->setGroup("ListenerConfig");
	m_enabled = m_config->readBoolEntry("enabled_" + m_serviceName, 
					    m_enabled);
	QDateTime nullTime;
	m_expirationTime = m_config->readDateTimeEntry("enabled_expiration_"+m_serviceName, 
						     &nullTime);
	if ((!m_expirationTime.isNull()) && (m_expirationTime < QDateTime::currentDateTime()))
		m_enabled = false;
}

void PortListener::accepted(KSocket *sock) {
	QString host, port;
	KSocketAddress *ksa = KExtendedSocket::peerAddress(sock->socket());
	KExtendedSocket::resolve(ksa, host, port);
	KNotifyClient::event("IncomingConnection",
		i18n("connection from %1").arg(host));
	delete ksa;

	if ((!m_enabled) ||
	   ((!m_multiInstance) && m_process.isRunning())) {
		delete sock;
		return;
	}

	m_process.clearArguments();
	m_process << m_execPath << m_argument << QString::number(sock->socket());
	if (!m_process.start(KProcess::DontCare)) {
		KNotifyClient::event("ProcessFailed",
			i18n("Call \"%1 %2 %3\" failed").arg(m_execPath)
				.arg(m_argument)
				.arg(sock->socket()));
	}

	delete sock;
}

bool PortListener::isValid() {
	return m_valid;
}

bool PortListener::isEnabled() {
	return m_enabled;
}

int PortListener::port() {
	return m_port;
}

void PortListener::setEnabled(bool e) {
	setEnabledInternal(e, QDateTime());
}

void PortListener::setEnabledInternal(bool e, const QDateTime &ex) {
	m_expirationTime = ex;
	if (e) {
		if (m_port < 0)
			acquirePort();
		if (m_port < 0) {
			m_enabled = false;
			return;
		}
	}
	else {
		m_port = -1;
		if (m_socket)
			delete m_socket;
		m_socket = 0;
	}

	m_enabled = e;

	m_config->setGroup("ListenerConfig");
	m_config->writeEntry("enabled_" + m_serviceName, m_enabled);
	m_config->writeEntry("enabled_expiration_"+m_serviceName, ex);
	m_config->sync();
}

void PortListener::setEnabled(const QDateTime &ex) {
	setEnabledInternal(true, ex);
}

QDateTime PortListener::expiration() {
	return m_expirationTime;
}

QString PortListener::name() {
	return m_serviceName;
}

PortListener::~PortListener() {
	if (m_socket)
		delete m_socket;
	if (m_config)
		delete m_config;
}


KInetD::KInetD(QCString &n) :
	KDEDModule(n)
{
	m_config = new KConfig("kinetdrc");
	m_portListeners.setAutoDelete(true);
	connect(&m_expirationTimer, SIGNAL(timeout()), SLOT(setTimer()));
	loadServiceList();
}

void KInetD::loadServiceList()
{
	m_portListeners.clear();


	KService::List kinetdModules =
		KServiceType::offers("KInetDModule");
	for(KService::List::ConstIterator it = kinetdModules.begin();
		it != kinetdModules.end();
		it++) {
		KService::Ptr s = *it;
		PortListener *pl = new PortListener(s, m_config);
		if (pl->isValid())
			m_portListeners.append(pl);
	}

	setTimer();
}

void KInetD::setTimer() {
	QDateTime nextEx = getNextExpirationTime(); // disables expired portlistener!
	if (!nextEx.isNull())
		m_expirationTimer.start(QDateTime::currentDateTime().secsTo(nextEx)*1000 + 30000,
			false);
	else
		m_expirationTimer.stop();
}

PortListener *KInetD::getListenerByName(QString name)
{
	PortListener *pl = m_portListeners.first();
	while (pl) {
		if (pl->name() == name)
			return pl;
		pl = m_portListeners.next();
	}
	return pl;
}

// gets next expiration timer, SIDEEFFECT: disables expired portlisteners while doing this
QDateTime KInetD::getNextExpirationTime()
{
	PortListener *pl = m_portListeners.first();
	QDateTime d;
	while (pl) {
		QDateTime d2 = pl->expiration();
		if (!d2.isNull()) {
			if (d2 < QDateTime::currentDateTime())
				pl->setEnabled(false);
			else if (d.isNull() || (d2 < d))
				d = d2;
		}
		pl = m_portListeners.next();
	}
	return d;
}

QStringList KInetD::services()
{
	QStringList list;
	PortListener *pl = m_portListeners.first();
	while (pl) {
		list.append(pl->name());
		pl = m_portListeners.next();
	}
	return list;
}

bool KInetD::isEnabled(QString service)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return false;

	return pl->isEnabled();
}

int KInetD::port(QString service)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return -1;

	return pl->port();
}

bool KInetD::isInstalled(QString service)
{
	PortListener *pl = getListenerByName(service);
	return (pl != 0);
}

void KInetD::setEnabled(QString service, bool enable)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return;

	pl->setEnabled(enable);
	setTimer();
}

void KInetD::setEnabled(QString service, QDateTime expiration)
{
	PortListener *pl = getListenerByName(service);
	if (!pl)
		return;

	pl->setEnabled(expiration);
	setTimer();
}


extern "C" {
	KDEDModule *create_kinetd(QCString &name)
	{
		return new KInetD(name);
	}
}




