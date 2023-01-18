#include "library/objective-c/proxy/EnvoyProxySettings.h"

NS_ASSUME_NONNULL_BEGIN

@interface EnvoyPACProxyResolver : NSObject

- (void)resolveProxiesForTargetURL:(NSURL *)targetURL
          proxyAutoConfigurationURL:(NSURL *)proxyAutoConfigurationURL
                withCompletionBlock:(void(^)(NSArray<EnvoyProxySettings *> * _Nullable, NSError * _Nullable))completion;
@end

NS_ASSUME_NONNULL_END
