module github.com/envoyproxy/envoy

go 1.13

require (
	github.com/envoyproxy/envoy/examples/grpc-bridge/server v0.0.0-20231214044345-b4fba1a3cd22
	golang.org/x/net v0.19.0
	google.golang.org/grpc v1.60.0
)

replace github.com/envoyproxy/envoy/examples/grpc-bridge/server/kv => ./kv
