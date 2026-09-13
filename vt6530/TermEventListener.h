/*
 *   This file is part of the Standard Portable Library (SPL).
 *
 *   SPL is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Foobar is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with SPL.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _term_event_listener_h
#define _term_event_listener_h


class Vt6530EventListener
{
public:
	/**
	 *  The terminal has successfully connected
	 *  to the host.
	 */
	virtual void Vt6530_OnConnect() = 0;
	
	/**
	 *  The connection to the host was lost or
	 *  closed.
	 */
	virtual void Vt6530_OnDisconnect() = 0;

	virtual void Vt6530_OnResetLine() = 0;

	/**
	 *  The host has completed rendering the
	 *  screen and is now waiting for input.
	 */
	virtual void Vt6530_OnEnquire() = 0;
	
	/**
	 *  Changes in the display require the container
	 *  to repaint.
	 */
	virtual void Vt6530_OnDisplayChanged() = 0;
	
	/**
	 *  There has been an internal error.
	 */
	virtual void Vt6530_OnError(const char *message) = 0;
	
	/**
	 *  Debuging output -- may be ignored
	 */
	virtual void Vt6530_OnDebug(const char *message) = 0;

	virtual void Vt6530_OnRecv34(const char *op, const char *params, const int paramLen) = 0;

	virtual void Vt6530_OnTextWatch(const char *txt, const int commandCode) = 0;
};

#endif
