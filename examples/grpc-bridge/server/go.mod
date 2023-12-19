module github.com/envoyproxy/envoy

go 1.13

require (
	github.com/envoyproxy/envoy/examples/grpc-bridge/server v0.0.0-20231219043605-7e834d7123f3
	golang.org/x/net v0.19.0
	google.golang.org/grpc v1.60.1
)

replace github.com/envoyproxy/envoy/examples/grpc-bridge/server/kv => ./kv
