#!/bin/bash

# 参数说明：
#   $1 = 服务器公网 IP
#   $2 = 掩码（例如 24）
#   $3 = 域名（与 turnserver.conf 中的 realm 一致）
#   $4 = TURN 用户名
#   $5 = TURN 密码

SERVER_IP="$1"
NETMASK="$2"
DOMAIN="$3"
TURN_USER="$4"
TURN_PASS="$5"

if [ -z "$SERVER_IP" ] || [ -z "$NETMASK" ] || [ -z "$DOMAIN" ] || [ -z "$TURN_USER" ] || [ -z "$TURN_PASS" ]; then
  echo "用法: $0 <服务器公网IP> <掩码> <域名> <TURN用户名> <TURN密码>"
  echo "示例: $0 1.2.3.4 24 turn.example.com turnuser turnpass"
  exit 1
fi

# 生成证书（如已有可自行注释掉）
openssl req -x509 -newkey rsa:1024 -keyout /etc/ssl/turn_key.pem -out /etc/ssl/turn_cert.pem -days 9999 -nodes

# 云服务器的公网ip绑定到服务器的eth0网卡上
ip addr add "${SERVER_IP}/${NETMASK}" dev eth0

mkdir -p /www/dockerfiles/coturn

# 使用模板 turnserver.conf 进行变量替换
sed -e "s/SEVER_IP/${SERVER_IP}/g" \
    -e "s/DOMAIN_NAME/${DOMAIN}/g" \
    -e "s/USERNAME:PASSWORD/${TURN_USER}:${TURN_PASS}/g" \
    turnserver.conf > /www/dockerfiles/coturn/turnserver.conf

cp docker-compose.yml /www/dockerfiles/coturn/docker-compose.yml

docker-compose -f /www/dockerfiles/coturn/docker-compose.yml up -d