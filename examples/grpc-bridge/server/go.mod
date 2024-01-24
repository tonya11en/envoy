module github.com/envoyproxy/envoy

go 1.13

require (
	github.com/envoyproxy/envoy/examples/grpc-bridge/server v0.0.0-20240124035555-282ff3a2ea44
	golang.org/x/net v0.20.0
	google.golang.org/grpc v1.61.0
)

replace github.com/envoyproxy/envoy/examples/grpc-bridge/server/kv => ./kv
