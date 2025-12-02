## 搭建私人的TURN服务器
### 0. 安装Docker，自行搜索教程
### 1. 了解启动参数
```bash
./install.sh 1.2.3.4 24 turn.example.com turnuser turnpass
```
对应关系：
- **服务器公网 IP**: `1.2.3.4`
- **掩码**: `24`（即 `1.2.3.4/24`）
- **域名**: `turn.example.com`（会替换 `realm=你的域名`）
- **TURN 用户名**: `turnuser`
- **TURN 密码**: `turnpass`（会组合成 `用户名:密码`）
### 2. 启动TURN服务器
```bash
chmod +x install.sh
./install.sh 1.2.3.4 24 turn.example.com turnuser turnpass
```

> **注意**：
> 服务器一般都没有绑定公网IP，install.sh使用了临时绑定命令
```bash
# 临时绑定云服务器的公网ip绑定到服务器的eth0网卡上
ip addr add "${SERVER_IP}/${NETMASK}" dev eth0
```
绑定后效果：
```bash
[root@VM coturn]# ip a
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq state UP group default qlen 1000
    link/ether 52:54:00:6b:6a:6f brd ff:ff:ff:ff:ff:ff
    altname enp0s5
    altname ens5
    inet 你的公网IP/24 scope global eth0
       valid_lft forever preferred_lft forever

```
