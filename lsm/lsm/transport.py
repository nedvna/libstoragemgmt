#!/usr/bin/env python

# Copyright (c) 2011, Red Hat, Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in
#      the documentation and/or other materials provided with the
#      distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
# IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
# TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import json
import socket
import string
from common import SocketEOF, LsmError
from data import DataDecoder, DataEncoder
import unittest
import threading

class Transport(object):
    """
    Provides wire serialization by using json.  Loosely conforms to json-rpc,
    however a length header was added so that we would have the ability to use
    non sax like json parsers, which are more abundant.

    <Zero padded 10 digit number [1..2**32] for the length followed by
    valid json.

    Notes:
    id field (json-rpc) is present but currently not being used.
    This is available to be expanded on later.
    """

    HDR_LEN = 10

    def __readAll( self, l ):
        """
        Reads l number of bytes before returning.  Will raise a SocketEOF
        if socket returns zero bytes (i.e. socket no longer connected)
        """

        if l < 1:
            raise ValueError("Trying to read less than 1 byte!")

        data = ""
        while len(data) < l:
            r = self.s.recv(l - len(data))
            if not r:
                raise SocketEOF()
            data += r

        return data

    def __sendMsg(self, msg):
        """
        Sends the json formatted message by pre-appending the length
        first.
        """

        if msg is None or len(msg) < 1:
            raise ValueError("Msg argument empty")

        self.s.sendall(string.zfill(len(msg), self.HDR_LEN) + msg)

    def __recvMsg(self):
        """
        Reads header first to get the length and then the remaining
        bytes of the message.
        """
        l = self.__readAll(self.HDR_LEN)
        return self.__readAll(int(l))

    def __init__(self, socket):
        self.s = socket

    @staticmethod
    def getSocket(path):
        """
        Returns a connected socket from the passed in path.
        """
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(path)
        return s

    def close(self):
        """
        Closes the transport and the underlying socket
        """
        self.s.close()

    def send_req(self, method, args):
        """
        Sends a request given a method and arguments.
        Note: arguments must be in the form that can be automatically
        serialized to json
        """
        msg = {'method': method, 'id': 100, 'params': args}
        data = json.dumps(msg, cls=DataEncoder)
        self.__sendMsg(data)

    def read_req(self):
        """
        Reads a message and returns the parsed version of it.
        """
        data = self.__recvMsg()
        if len(data):
            return json.loads(data, cls=DataDecoder)

    def rpc(self, method, args):
        """
        Sends a request and waits for a response.
        """
        self.send_req(method, args)
        (reply, id) = self.read_resp()
        assert id == 100
        return reply

    def send_error(self, id, error_code, msg, data=None):
        """
        Used to transmit an error.
        """
        e = {'id': id, 'error': {'code': error_code,
                                 'message': msg,
                                 'data': data}}
        self.__sendMsg(json.dumps(e, cls=DataEncoder))

    def send_resp(self, result, id=100):
        """
        Used to transmit a response
        """

        r = {'id': id, 'result': result}
        self.__sendMsg(json.dumps(r, cls=DataEncoder))

    def read_resp(self):
        data = self.__recvMsg()
        resp = json.loads(data, cls=DataDecoder)

        if 'result' in resp:
            return resp['result'], resp['id']
        else:
            e = resp['error']
            raise LsmError(**e)


def server(s):
    """
    Test echo server for test case.
    """
    server = Transport(s)

    msg = server.read_req()

    try:
        while( msg['method'] != 'done' ):

            if msg['method'] == 'error':
                server.send_error(msg['id'], msg['params']['errorcode'],
                                    msg['params']['errormsg'])
            else:
                server.send_resp(msg['params'])
            msg = server.read_req()
        server.send_resp(msg['params'])
    finally:
        s.close()


class TestTransport(unittest.TestCase):
    def setUp(self):
        (self.c,self.s) = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)

        self.client = Transport(self.c)

        self.server = threading.Thread(target=server, args=(self.s,))
        self.server.start()

    def test_simple(self):
        tc = [ '0', ' ', '   ', '{}:""', "Some text message", 'DEADBEEF']

        for t in tc:
            self.client.send_req('test', t)
            reply, id = self.client.read_resp()
            self.assertTrue( id == 100)
            self.assertTrue( reply == t )


    def test_exceptions(self):

        e_msg = 'Test error message'
        e_code = 100

        self.client.send_req('error', {'errorcode':e_code, 'errormsg':e_msg} )
        self.assertRaises(LsmError, self.client.read_resp)

        try:
            self.client.send_req('error', {'errorcode':e_code, 'errormsg':e_msg} )
            self.client.read_resp()
        except LsmError as e:
            self.assertTrue(e.code == e_code)
            self.assertTrue(e.msg == e_msg)

    def test_slow(self):

        #Try to test the receiver getting small chunks to read
        #in a loop
        for l in range(1, 4096, 10 ):

            payload = "x" * l
            msg = {'method': 'drip', 'id': 100, 'params': payload}
            data = json.dumps(msg, cls=DataEncoder)

            wire = string.zfill(len(data), Transport.HDR_LEN) + data

            self.assertTrue(len(msg) >= 1)

            for i in wire:
                self.c.send(i)

            reply, id = self.client.read_resp()
            self.assertTrue(payload == reply)

    def tearDown(self):
        self.client.send_req("done", None)
        resp, id = self.client.read_resp()
        self.assertTrue(resp == None)
        self.server.join()


if __name__ == "__main__":
    unittest.main()