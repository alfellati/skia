/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "include/core/SkColorSpace.h"
#include "include/core/SkData.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkTypes.h"
#include "include/encode/SkICC.h"
#include "modules/skcms/skcms.h"
#include "tests/Test.h"
#include "tools/Resources.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>

DEF_TEST(AdobeRGB, r) {
    if (sk_sp<SkData> profile = GetResourceAsData("icc_profiles/AdobeRGB1998.icc")) {
        skcms_ICCProfile parsed;
        REPORTER_ASSERT(r, skcms_Parse(profile->data(), profile->size(), &parsed));
        REPORTER_ASSERT(r, !parsed.has_CICP);

        auto got  = SkColorSpace::Make(parsed);
        auto want = SkColorSpace::MakeRGB(SkNamedTransferFn::k2Dot2, SkNamedGamut::kAdobeRGB);
        REPORTER_ASSERT(r, SkColorSpace::Equals(got.get(), want.get()));
    }
}

DEF_TEST(HDR_ICC, r) {
    constexpr size_t kTestCount = 3;
    sk_sp<SkData> profile[kTestCount] = {
            SkWriteICCProfile(SkNamedTransferFn::kPQ, SkNamedGamut::kRec2020),
            SkWriteICCProfile(SkNamedTransferFn::kHLG, SkNamedGamut::kDisplayP3),
            SkWriteICCProfile(SkNamedTransferFn::kSRGB, SkNamedGamut::kSRGB),
    };

    sk_sp<SkData> dst_profile[kTestCount] = {
            SkWriteICCProfile(SkNamedTransferFn::kLinear, SkNamedGamut::kRec2020),
            SkWriteICCProfile(SkNamedTransferFn::kLinear, SkNamedGamut::kDisplayP3),
            SkWriteICCProfile(SkNamedTransferFn::kLinear, SkNamedGamut::kSRGB),
    };

    constexpr size_t kPixelCount = 6;

    // clang-format off
    float pixels[kPixelCount][3] = {
            { 0.00f, 0.00f, 0.00f, },
            { 0.50f, 0.50f, 0.50f, },
            { 0.50f, 0.00f, 0.00f, },
            { 0.00f, 0.50f, 0.00f, },
            { 0.00f, 0.00f, 0.50f, },
            { 0.75f, 0.75f, 0.75f, },
    };
    float dst_pixels_expected[kTestCount][kPixelCount][3] = {
            {
                    { 0.f,     0.f,     0.f,     },
                    { 0.2885f, 0.2885f, 0.2885f, },
                    { 0.2885f, 0.f,     0.f,     },
                    { 0.f,     0.2885f, 0.f,     },
                    { 0.f,     0.f,     0.2885f, },
                    { 0.9910f, 0.9910f, 0.9910f, }, // PQ maps 0.75 ~ 1000 nits to 1.0
            },
            {
                    { 0.f,     0.f,     0.f,     },
                    { 0.1436f, 0.1539f, 0.1333f, }, // HLG's OOTF results in R != G != B
                    { 0.1324f, 0.f,     0.f,     },
                    { 0.f,     0.1506f, 0.f,     },
                    { 0.f,     0.f,     0.1066f, },
                    { 0.4929f, 0.4938f, 0.4924f, },
            },
            {
                    { 0.f,     0.0f,    0.0f,    },
                    { 0.2140f, 0.2140f, 0.2140f, }, // This is just the sRGB transfer function
                    { 0.2140f, 0.0f,    0.0f,    },
                    { 0.0f,    0.2140f, 0.0f,    },
                    { 0.0f,    0.0f,    0.2140f, },
                    { 0.5225f, 0.5225f, 0.5225f, },
            },
    };
    // clang-format on
    bool cicp_expected[kTestCount] = {true, true, false};
    bool a2b_expected[kTestCount] = {true, true, false};
    uint32_t cicp_primaries_expected[kTestCount] = {9, 12, 0};
    uint32_t cicp_trfn_expected[kTestCount] = {16, 18, 0};

    for (size_t test = 0; test < kTestCount; ++test) {
        skcms_ICCProfile parsed;
        REPORTER_ASSERT(r, skcms_Parse(profile[test]->data(), profile[test]->size(), &parsed));

        REPORTER_ASSERT(r, parsed.has_A2B == a2b_expected[test]);
        REPORTER_ASSERT(r, parsed.has_CICP == cicp_expected[test]);
        if (cicp_expected[test]) {
            REPORTER_ASSERT(r, parsed.CICP.color_primaries == cicp_primaries_expected[test]);
            REPORTER_ASSERT(r, parsed.CICP.transfer_characteristics == cicp_trfn_expected[test]);
            REPORTER_ASSERT(r, parsed.CICP.matrix_coefficients == 0);
            REPORTER_ASSERT(r, parsed.CICP.video_full_range_flag == 1);
        }

        skcms_ICCProfile dst_parsed;
        REPORTER_ASSERT(
                r, skcms_Parse(dst_profile[test]->data(), dst_profile[test]->size(), &dst_parsed));

        for (size_t pixel = 0; pixel < kPixelCount; ++pixel) {
            float dst_pixel_actual[3]{0.f, 0.f, 0.f};
            bool xform_result = skcms_Transform(pixels[pixel],
                                                skcms_PixelFormat_RGB_fff,
                                                skcms_AlphaFormat_Opaque,
                                                &parsed,
                                                dst_pixel_actual,
                                                skcms_PixelFormat_RGB_fff,
                                                skcms_AlphaFormat_Opaque,
                                                &dst_parsed,
                                                1);
            REPORTER_ASSERT(r, xform_result);

            auto approx_equal = [=](float x, float y) { return std::abs(x - y) < 1e-2f; };
            for (size_t i = 0; i < 3; ++i) {
                REPORTER_ASSERT(
                        r, approx_equal(dst_pixel_actual[i], dst_pixels_expected[test][pixel][i]));
            }
        }
    }
}
