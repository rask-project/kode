#include "FileTypes.h"

#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

namespace kode
{

bool isKnownNonTextType(const QString &fileName)
{
    NSString *const extension{fileName.toNSString().pathExtension};
    if (extension.length == 0) {
        return false;
    }
    UTType *const type{[UTType typeWithFilenameExtension:extension]};
    return type != nil && !type.dynamic && ![type conformsToType:UTTypeText];
}

} // namespace kode
