#!/usr/bin/perl

# Written by Martin Kollaard

use strict;
use warnings;
use JSON;

use ZMQ::LibZMQ3;
use ZMQ::Constants qw(ZMQ_SUB ZMQ_SUBSCRIBE ZMQ_IPV4ONLY);
use POSIX qw( strftime );

# Prepare our context and subscriber
my $context = zmq_init();
my $subscriber = zmq_socket($context, ZMQ_SUB);
zmq_setsockopt($subscriber, ZMQ_IPV4ONLY, 0);
zmq_connect($subscriber, 'tcp://127.0.0.1:5555');
zmq_setsockopt($subscriber, ZMQ_SUBSCRIBE, '');
while (1) {
        my $msg = zmq_recvmsg($subscriber);
        my $json = zmq_msg_data($msg);
        my $data = from_json($json);
        print strftime("%Y-%m-%d %H:%M:%S", localtime($data->{data}->{timestamp})) ."\t";
        if ($data->{extra}->{isGroupMessage}) {
                print " GROUP\t  ";
        } else {
                print " ALPHA\t  ";
        }
        print $data->{data}->{message} . "\n";

        foreach my $capcode (@{$data->{data}->{capcodes}}) {
                print "\t\t\t" . $capcode . "\n";
        }
        print "\n";
}
