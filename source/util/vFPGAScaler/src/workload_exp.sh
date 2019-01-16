#!/bin/bash


eval 'rm -rf ../log/*.log'
eval './scheduler $1 $2 $3'
eval 'mkdir ../log/WorkLoadExp_$4'
eval 'mv ../log/*.log ../log/WorkLoadExp_$4'
