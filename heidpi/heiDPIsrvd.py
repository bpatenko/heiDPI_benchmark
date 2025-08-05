#!/usr/bin/env python3

import json
import re
import os
import socket
import sys

NETWORK_BUFFER_MIN_SIZE = 6 # NETWORK_BUFFER_LENGTH_DIGITS + 1
NETWORK_BUFFER_MAX_SIZE = 33792 # Please keep this value in sync with the one in config.h
nDPId_PACKETS_PLEN_MAX = 8192 # Please keep this value in sync with the one in config.h

PKT_TYPE_ETH_IP4 = 0x0800
PKT_TYPE_ETH_IP6 = 0x86DD

class ThreadData:
    pass

class Instance:

    def __init__(self, alias, source):
        self.alias = str(alias)
        self.source = str(source)
        self.flows = dict()
        self.thread_data = dict()

    def __str__(self):
        return '<%s.%s object at %s with alias %s, source %s>' % (
            self.__class__.__module__,
            self.__class__.__name__,
            hex(id(self)),
            self.alias,
            self.source
        )

    def getThreadData(self, thread_id):
        if thread_id not in self.thread_data:
            return None
        return self.thread_data[thread_id]

    def getThreadDataFromJSON(self, json_dict):
        if 'thread_id' not in json_dict:
            return None
        return self.getThreadData(json_dict['thread_id'])

    def getMostRecentFlowTime(self, thread_id):
        return self.thread_data[thread_id].most_recent_flow_time

    def setMostRecentFlowTime(self, thread_id, most_recent_flow_time):
        if thread_id in self.thread_data:
            return self.thread_data[thread_id]

        self.thread_data[thread_id] = ThreadData()
        self.thread_data[thread_id].most_recent_flow_time = most_recent_flow_time
        return self.thread_data[thread_id]

    def getMostRecentFlowTimeFromJSON(self, json_dict):
        if 'thread_id' not in json_dict:
            return 0
        return self.getThreadData(json_dict['thread_id']).most_recent_flow_time

    def setMostRecentFlowTimeFromJSON(self, json_dict):
        if 'thread_id' not in json_dict:
            return
        thread_id = json_dict['thread_id']
        if 'thread_ts_usec' in json_dict:
            mrtf = self.getMostRecentFlowTime(thread_id) if thread_id in self.thread_data else 0
            self.setMostRecentFlowTime(thread_id, max(json_dict['thread_ts_usec'], mrtf))

class Flow:

    def __init__(self, flow_id, thread_id):
        self.flow_id = flow_id
        self.thread_id = thread_id
        self.flow_last_seen = -1
        self.flow_idle_time = -1
        self.cleanup_reason = -1

    def __str__(self):
        return '<%s.%s object at %s with flow id %d>' % (
            self.__class__.__module__,
            self.__class__.__name__,
            hex(id(self)),
            self.flow_id
        )

class FlowManager:
    CLEANUP_REASON_INVALID         = 0
    CLEANUP_REASON_DAEMON_INIT     = 1 # can happen if kill -SIGKILL $(pidof nDPId) or restart after SIGSEGV
    CLEANUP_REASON_DAEMON_SHUTDOWN = 2 # graceful shutdown e.g. kill -SIGTERM $(pidof nDPId)
    CLEANUP_REASON_FLOW_END        = 3
    CLEANUP_REASON_FLOW_IDLE       = 4
    CLEANUP_REASON_FLOW_TIMEOUT    = 5 # nDPId died a long time ago w/o restart?
    CLEANUP_REASON_APP_SHUTDOWN    = 6 # your python app called FlowManager.doShutdown()

    def __init__(self):
        self.instances = dict()

    def getInstance(self, json_dict):
        if 'alias' not in json_dict or \
           'source' not in json_dict:
            return None

        alias  = json_dict['alias']
        source = json_dict['source']

        if alias not in self.instances:
            self.instances[alias] = dict()
        if source not in self.instances[alias]:
            self.instances[alias][source] = dict()
            self.instances[alias][source] = Instance(alias, source)

        self.instances[alias][source].setMostRecentFlowTimeFromJSON(json_dict)

        return self.instances[alias][source]

    @staticmethod
    def getLastPacketTime(instance, flow_id, json_dict):
        return max(int(json_dict['flow_src_last_pkt_time']), int(json_dict['flow_dst_last_pkt_time']), instance.flows[flow_id].flow_last_seen)

    def getFlow(self, instance, json_dict):
        if 'flow_id' not in json_dict:
            return None

        flow_id = int(json_dict['flow_id'])

        if flow_id in instance.flows:
            instance.flows[flow_id].flow_last_seen = FlowManager.getLastPacketTime(instance, flow_id, json_dict)
            instance.flows[flow_id].flow_idle_time = int(json_dict['flow_idle_time'])
            return instance.flows[flow_id]

        thread_id = int(json_dict['thread_id'])
        instance.flows[flow_id] = Flow(flow_id, thread_id)
        instance.flows[flow_id].flow_last_seen = FlowManager.getLastPacketTime(instance, flow_id, json_dict)
        instance.flows[flow_id].flow_idle_time = int(json_dict['flow_idle_time'])
        instance.flows[flow_id].cleanup_reason = FlowManager.CLEANUP_REASON_INVALID

        return instance.flows[flow_id]

    def getFlowsToCleanup(self, instance, json_dict):
        flows = dict()

        if 'daemon_event_name' in json_dict:
            if json_dict['daemon_event_name'].lower() == 'init' or \
               json_dict['daemon_event_name'].lower() == 'shutdown':
                # invalidate all existing flows with that alias/source/thread_id
                for flow_id in instance.flows:
                    flow = instance.flows[flow_id]
                    if flow.thread_id != int(json_dict['thread_id']):
                        continue
                    if json_dict['daemon_event_name'].lower() == 'init':
                        flow.cleanup_reason = FlowManager.CLEANUP_REASON_DAEMON_INIT
                    else:
                        flow.cleanup_reason = FlowManager.CLEANUP_REASON_DAEMON_SHUTDOWN
                    flows[flow_id] = flow
                for flow_id in flows:
                    del instance.flows[flow_id]
                if len(instance.flows) == 0:
                    del self.instances[instance.alias][instance.source]

        elif 'flow_event_name' in json_dict and \
           (json_dict['flow_event_name'].lower() == 'end' or \
            json_dict['flow_event_name'].lower() == 'idle' or \
            json_dict['flow_event_name'].lower() == 'guessed' or \
            json_dict['flow_event_name'].lower() == 'not-detected' or \
            json_dict['flow_event_name'].lower() == 'detected'):
            flow_id = json_dict['flow_id']
            if json_dict['flow_event_name'].lower() == 'end':
                instance.flows[flow_id].cleanup_reason = FlowManager.CLEANUP_REASON_FLOW_END
            elif json_dict['flow_event_name'].lower() == 'idle':
                instance.flows[flow_id].cleanup_reason = FlowManager.CLEANUP_REASON_FLOW_IDLE
            # TODO: Flow Guessing/Detection can happen right before an idle event.
            #       We need to prevent that it results in a CLEANUP_REASON_FLOW_TIMEOUT.
            #       This may cause inconsistency and needs to be handled in another way.
            if json_dict['flow_event_name'].lower() != 'guessed' and \
               json_dict['flow_event_name'].lower() != 'not-detected' and \
               json_dict['flow_event_name'].lower() != 'detected':
                flows[flow_id] = instance.flows.pop(flow_id)

        elif 'flow_last_seen' in json_dict:
            if int(json_dict['flow_last_seen']) + int(json_dict['flow_idle_time']) < \
               instance.getMostRecentFlowTimeFromJSON(json_dict):
                flow_id = json_dict['flow_id']
                instance.flows[flow_id].cleanup_reason = FlowManager.CLEANUP_REASON_FLOW_TIMEOUT
                flows[flow_id] = instance.flows.pop(flow_id)

        return flows

    def doShutdown(self):
        flows = dict()

        for alias in self.instances:
            for source in self.instances[alias]:
                for flow_id in self.instances[alias][source].flows:
                    flow = self.instances[alias][source].flows[flow_id]
                    flow.cleanup_reason = FlowManager.CLEANUP_REASON_APP_SHUTDOWN
                    flows[flow_id] = flow

        del self.instances

        return flows

    def verifyFlows(self):
        invalid_flows = list()

        for alias in self.instances:
            for source in self.instances[alias]:
                for flow_id in self.instances[alias][source].flows:
                    thread_id = self.instances[alias][source].flows[flow_id].thread_id
                    if self.instances[alias][source].flows[flow_id].flow_last_seen + \
                       self.instances[alias][source].flows[flow_id].flow_idle_time < \
                       self.instances[alias][source].getMostRecentFlowTime(thread_id):
                        invalid_flows += [flow_id]

        return invalid_flows

class nDPIsrvdException(Exception):
    UNSUPPORTED_ADDRESS_TYPE = 1
    BUFFER_CAPACITY_REACHED  = 2
    SOCKET_CONNECTION_BROKEN = 3
    INVALID_LINE_RECEIVED    = 4
    CALLBACK_RETURNED_FALSE  = 5
    SOCKET_TIMEOUT           = 6

    def __init__(self, etype):
        self.etype = etype
    def __str__(self):
        return 'nDPIsrvdException type {}'.format(self.etype)

class UnsupportedAddressType(nDPIsrvdException):
    def __init__(self, addr):
        super().__init__(nDPIsrvdException.UNSUPPORTED_ADDRESS_TYPE)
        self.addr = addr
    def __str__(self):
        return '{}'.format(str(self.addr))

class BufferCapacityReached(nDPIsrvdException):
    def __init__(self, current_length, max_length):
        super().__init__(nDPIsrvdException.BUFFER_CAPACITY_REACHED)
        self.current_length = current_length
        self.max_length = max_length
    def __str__(self):
        return '{} of {} bytes'.format(self.current_length, self.max_length)

class SocketConnectionBroken(nDPIsrvdException):
    def __init__(self):
        super().__init__(nDPIsrvdException.SOCKET_CONNECTION_BROKEN)
    def __str__(self):
        return 'Disconnected.'

class InvalidLineReceived(nDPIsrvdException):
    def __init__(self, packet_buffer):
        super().__init__(nDPIsrvdException.INVALID_LINE_RECEIVED)
        self.packet_buffer = packet_buffer
    def __str__(self):
        return 'Received JSON line is invalid.'

class CallbackReturnedFalse(nDPIsrvdException):
    def __init__(self):
        super().__init__(nDPIsrvdException.CALLBACK_RETURNED_FALSE)
    def __str__(self):
        return 'Callback returned False, abort.'

class SocketTimeout(nDPIsrvdException):
    def __init__(self):
        super().__init__(nDPIsrvdException.SOCKET_TIMEOUT)
    def __str__(self):
        return 'Socket timeout.'

class JsonFilter():
    def __init__(self, filter_string):
        self.filter_string = filter_string
        self.filter = compile(filter_string, '<string>', 'eval')
    def evaluate(self, json_dict):
        if type(json_dict) is not dict:
            raise nDPIsrvdException('Could not evaluate JSON Filter: expected dictionary, got {}'.format(type(json_dict)))
        return eval(self.filter, {'json_dict': json_dict})

class nDPIsrvdSocket:
    def __init__(self):
        self.sock_family = None
        self.flow_mgr = FlowManager()
        self.received_bytes = 0
        self.json_filter = list()

    def addFilter(self, filter_str):
        self.json_filter.append(JsonFilter(filter_str))

    def evalFilters(self, json_dict):
        for jf in self.json_filter:
            try:
                json_filter_retval = jf.evaluate(json_dict)
            except Exception as err:
                print()
                sys.stderr.write('Error while evaluating expression "{}"\n'.format(jf.filter_string))
                raise err

            if not isinstance(json_filter_retval, bool):
                print()
                sys.stderr.write('Error while evaluating expression "{}"\n'.format(jf.filter_string))
                raise nDPIsrvdException('JSON Filter returned an invalid type: expected bool, got {}'.format(type(json_filter_retval)))

            if json_filter_retval is False:
                return False

        return True

    def connect(self, addr):
        if type(addr) is tuple:
            self.sock_family = socket.AF_INET
        elif type(addr) is str:
            self.sock_family = socket.AF_UNIX
        else:
            raise UnsupportedAddressType(addr)

        self.sock = socket.socket(self.sock_family, socket.SOCK_STREAM)
        self.sock.connect(addr)
        self.buffer = bytes()
        self.msglen = 0
        self.digitlen = 0
        self.lines = []
        self.failed_lines = []
        self.filtered_lines = 0

    def timeout(self, timeout):
        self.sock.settimeout(timeout)

    def receive(self):
        if len(self.buffer) == NETWORK_BUFFER_MAX_SIZE:
            raise BufferCapacityReached(len(self.buffer), NETWORK_BUFFER_MAX_SIZE)

        connection_finished = False
        try:
            recvd = self.sock.recv(NETWORK_BUFFER_MAX_SIZE - len(self.buffer))
        except ConnectionResetError:
            connection_finished = True
            recvd = bytes()
        except TimeoutError:
            raise SocketTimeout()
        except socket.timeout:
            raise SocketTimeout()

        if len(recvd) == 0:
            connection_finished = True

        self.buffer += recvd

        new_data_avail = False
        while self.msglen + self.digitlen <= len(self.buffer):

            if self.msglen == 0:
                starts_with_digits = re.match(r'(^\d+){', self.buffer[:NETWORK_BUFFER_MIN_SIZE].decode(errors='strict'))
                if starts_with_digits is None:
                    if len(self.buffer) < NETWORK_BUFFER_MIN_SIZE:
                        break
                    raise InvalidLineReceived(self.buffer)
                self.msglen = int(starts_with_digits.group(1))
                self.digitlen = len(starts_with_digits.group(1))

            if len(self.buffer) >= self.msglen + self.digitlen:
                recvd = self.buffer[self.digitlen:self.msglen + self.digitlen]
                self.buffer = self.buffer[self.msglen + self.digitlen:]
                self.lines += [(recvd,self.msglen,self.digitlen)]
                new_data_avail = True

                self.received_bytes += self.msglen + self.digitlen
                self.msglen = 0
                self.digitlen = 0

        if connection_finished is True:
            raise SocketConnectionBroken()

        return new_data_avail

    def parse(self, callback_json, callback_flow_cleanup, global_user_data):
        retval = True

        for received_line in self.lines:
            try:
                json_dict = json.loads(received_line[0].decode('ascii', errors='replace'), strict=True)
            except json.decoder.JSONDecodeError as e:
                json_dict = dict()
                self.failed_lines += [received_line]
                self.lines = self.lines[1:]
                raise(e)

            instance = self.flow_mgr.getInstance(json_dict)
            if instance is None:
                self.failed_lines += [received_line]
                retval = False
                continue

            current_flow = self.flow_mgr.getFlow(instance, json_dict)
            filter_eval = self.evalFilters(json_dict)
            if filter_eval is True:
                try:
                    if callback_json(json_dict, instance, current_flow, global_user_data) is not True:
                        self.failed_lines += [received_line]
                        retval = False
                except Exception as e:
                        self.failed_lines += [received_line]
                        self.lines = self.lines[1:]
                        raise(e)
            else:
                self.filtered_lines += 1

            for _, flow in self.flow_mgr.getFlowsToCleanup(instance, json_dict).items():
                if callback_flow_cleanup is None:
                    pass
                elif filter_eval is True and callback_flow_cleanup(instance, flow, global_user_data) is not True:
                    self.failed_lines += [received_line]
                    self.lines = self.lines[1:]
                    retval = False

            self.lines = self.lines[1:]

        return retval

    def loop(self, callback_json, callback_flow_cleanup, global_user_data):
        throw_ex = None

        while True:
            bytes_recv = 0
            try:
                bytes_recv = self.receive()
            except Exception as err:
                throw_ex = err

            if self.parse(callback_json, callback_flow_cleanup, global_user_data) is False:
                raise CallbackReturnedFalse()

            if throw_ex is not None:
                raise throw_ex

    def shutdown(self):
        return self.flow_mgr.doShutdown().items()

    def verify(self):
        if len(self.failed_lines) > 0:
            raise nDPIsrvdException('Failed lines > 0: {}'.format(len(self.failed_lines)))
        return self.flow_mgr.verifyFlows()


def toSeconds(usec):
    return usec / (1000 * 1000)

global schema
schema = {'packet_event_schema' : None, 'error_event_schema' : None, 'daemon_event_schema' : None, 'flow_event_schema' : None}

def initSchemaValidator(schema_dirs=[]):
    if len(schema_dirs) == 0:
        schema_dirs += [os.path.dirname(sys.argv[0]) + '/schema']

    for key in schema:
        for schema_dir in schema_dirs:
            try:
                with open(schema_dir + '/' + str(key) + '.json', 'r') as schema_file:
                    schema[key] = json.load(schema_file)
            except FileNotFoundError:
                print(f"No schema in {schema_dir}")
                continue
            else:
                break

def validateAgainstSchema(json_dict):
    import jsonschema

    if 'packet_event_id' in json_dict:
        try:
            jsonschema.Draft7Validator(schema=schema['packet_event_schema']).validate(instance=json_dict)
        except AttributeError:
            jsonschema.validate(instance=json_dict, schema=schema['packet_event_schema'])
        return True
    if 'error_event_id' in json_dict:
        try:
            jsonschema.Draft7Validator(schema=schema['error_event_schema']).validate(instance=json_dict)
        except AttributeError:
            jsonschema.validate(instance=json_dict, schema=schema['error_event_schema'])
        return True
    if 'daemon_event_id' in json_dict:
        try:
            jsonschema.Draft7Validator(schema=schema['daemon_event_schema']).validate(instance=json_dict)
        except AttributeError:
            jsonschema.validate(instance=json_dict, schema=schema['daemon_event_schema'])
        return True
    if 'flow_event_id' in json_dict:
        try:
            jsonschema.Draft7Validator(schema=schema['flow_event_schema']).validate(instance=json_dict)
        except AttributeError:
            jsonschema.validate(instance=json_dict, schema=schema['flow_event_schema'])
        return True

    return False
