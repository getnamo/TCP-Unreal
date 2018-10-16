# tcp-ue4
Convenience ActorComponent TCP socket wrapper for Unreal Engine 4.

[![GitHub release](https://img.shields.io/github/release/getnamo/tcp-ue4.svg)](https://github.com/getnamo/udp-ue4/releases)
[![Github All Releases](https://img.shields.io/github/downloads/getnamo/tcp-ue4/total.svg)](https://github.com/getnamo/udp-ue4/releases)

Meant to be styled like https://github.com/getnamo/socketio-client-ue4 but for raw tcp sockets. Due to no dependencies this should work on a wider variety of platforms, but if your platform supports socket.io, I'd highly recommend using https://github.com/getnamo/socketio-client-ue4/releases instead.

## Quick Install & Setup

 1. [Download Latest Release](https://github.com/getnamo/tcp-ue4/releases)
 2. Create new or choose project.
 3. Browse to your project folder (typically found at Documents/Unreal Project/{Your Project Root})
 4. Copy *Plugins* folder into your Project root.
 5. Plugin should be now ready to use.
 
## How to use - Basics
 
_Not yet written_. For now see https://github.com/getnamo/udp-ue4#how-to-use---basics as this plugin follows the udp plugin concepts closely but with bi-directional sockets instead.

Need some simple test servers? Use [tcpEcho.js gist](https://gist.github.com/getnamo/7350f00823f46d9463240160320d03a3) to test ```TCPClientComponent``` and [tcpClient.js](https://gist.github.com/getnamo/396577cb4988188e291774ac7e368368) to test ```TCPServerComponent```.
