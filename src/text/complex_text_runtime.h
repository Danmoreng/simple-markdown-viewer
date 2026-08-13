#pragma once

#include <memory>
#include <string>

#include "include/core/SkRefCnt.h"

class SkFontMgr;
class SkShaper;
class SkUnicode;

namespace mdviewer {

class ComplexTextRuntime {
public:
    explicit ComplexTextRuntime(sk_sp<SkFontMgr> fontManager);
    ~ComplexTextRuntime();

    ComplexTextRuntime(const ComplexTextRuntime&) = delete;
    ComplexTextRuntime& operator=(const ComplexTextRuntime&) = delete;

    bool IsAvailable() const noexcept;
    const std::string& Diagnostic() const noexcept;

    SkUnicode* Unicode() const noexcept;
    SkShaper* Shaper() const noexcept;

private:
    sk_sp<SkUnicode> unicode_;
    std::unique_ptr<SkShaper> shaper_;
    std::string diagnostic_;
};

}  // namespace mdviewer
