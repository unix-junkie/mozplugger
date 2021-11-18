/**
 * This file is part of mozplugger a fork of plugger, for list of developers
 * see the README file.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111, USA.
 */
#ifndef _PIPE_MSG_H_
#define _PIPE_MSG_H_

struct Window_msg_s
{
     Window window;
     uint32_t width;
     uint32_t height;
};


struct Progress_msg_s
{
     uint8_t done;
     unsigned long bytes;
};


enum StateChg_e
{
     STOP_REQ,
     PLAY_REQ,
     PAUSE_REQ
};

struct StateChg_msg_s
{
     uint8_t stateChgReq;
};

/**
 * Format of messages passed from mozplugger.so to the mozplugger helpers
 */
struct PipeMsg_s
{
     uint8_t msgType;
     union
     {
           struct Window_msg_s window_msg;
           struct Progress_msg_s progress_msg;
           struct StateChg_msg_s stateChg_msg;
     };
};

typedef struct PipeMsg_s PipeMsg_t;

enum PipeMsg_e
{
     WINDOW_MSG,
     PROGRESS_MSG, /* file download progress */
     STATE_CHG_MSG, /* e.g. STOP, PAUSE, PLAY */
     SHUTDOWN_MSG /* Shutdown - nicer that sending a SIG TERM */
};

#endif
