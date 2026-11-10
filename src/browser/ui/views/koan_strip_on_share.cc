// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Contains material from Firefox (https://github.com/mozilla-firefox/firefox),
// MPL-2.0: the parameter and site lists of
//   toolkit/components/antitracking/StripOnShareLists/MPL2/StripOnShare.json
//   (at 3ee2103f63d355a42e58899a27b6b0483716c9d9)
// Firefox's LGPL list (StripOnShareLGPL.json) is not used. Koan's code,
// Copyright 2026 The Koan Authors, is under the same licence.

#include "koan/browser/ui/views/koan_strip_on_share.h"

#include <string>
#include <string_view>
#include <vector>

#include "base/containers/fixed_flat_set.h"
#include "base/containers/span.h"
#include "base/memory/raw_ptr_exclusion.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"

namespace koan {

namespace {

// A site's own parameters: for its hosts and their subdomains
// ("schemelessSites"), and for exact hosts ("hosts").
// RAW_PTR_EXCLUSION: #global-scope (rules live in a constexpr table).
struct SiteRule {
  RAW_PTR_EXCLUSION base::span<const std::string_view> params;
  RAW_PTR_EXCLUSION base::span<const std::string_view> sites;
  RAW_PTR_EXCLUSION base::span<const std::string_view> hosts;
};

constexpr auto kGlobalParams = base::MakeFixedFlatSet<std::string_view>({
    "ScCid",
    "__hsfp",
    "__hssc",
    "__hstc",
    "_branch_match_id",
    "_bta_c",
    "_bta_tid",
    "_ga",
    "_gl",
    "_hsenc",
    "_hsmi",
    "_ke",
    "_kx",
    "adgroupid",
    "adid",
    "adtype",
    "bg_aid_k",
    "bg_aid_v",
    "bg_campaign",
    "bg_kw",
    "bg_source",
    "bg_source_id",
    "channable",
    "cq_cmp",
    "cq_con",
    "cq_med",
    "cq_net",
    "cq_plac",
    "cq_plt",
    "cq_src",
    "cq_term",
    "cto_pld",
    "dm_i",
    "ef_id",
    "epik",
    "fbadid",
    "fbc_id",
    "fbclid",
    "gPromoCode",
    "gQT",
    "gad_campaignid",
    "gad_source",
    "gadid",
    "gbraid",
    "gclid",
    "gclsrc",
    "h_ad_id",
    "hsCtaTracking",
    "hsa_acc",
    "hsa_ad",
    "hsa_cam",
    "hsa_grp",
    "hsa_kw",
    "hsa_la",
    "hsa_mt",
    "hsa_net",
    "hsa_ol",
    "hsa_src",
    "hsa_tgt",
    "hsa_ver",
    "im_ref",
    "klar_adid",
    "klar_cpid",
    "klar_source",
    "li_fat_id",
    "matomo_campaign",
    "matomo_cid",
    "matomo_content",
    "matomo_group",
    "matomo_keyword",
    "matomo_medium",
    "matomo_placement",
    "matomo_source",
    "mc_cid",
    "mc_eid",
    "meta_placement",
    "meta_site_source",
    "mkwid",
    "msclkid",
    "mtm_campaign",
    "mtm_cid",
    "mtm_content",
    "mtm_group",
    "mtm_keyword",
    "mtm_medium",
    "mtm_placement",
    "mtm_source",
    "nb_adtype",
    "nb_ap",
    "nb_expid_meta",
    "nb_fii",
    "nb_klid",
    "nb_kwd",
    "nb_li_ms",
    "nb_lp_ms",
    "nb_mi",
    "nb_mt",
    "nb_pc",
    "nb_pi",
    "nb_placement",
    "nb_ppi",
    "nb_ti",
    "nbt",
    "ndclid",
    "padid",
    "pcrid",
    "piwik_campaign",
    "piwik_keyword",
    "piwik_kwd",
    "pk_campaign",
    "pk_cid",
    "pk_content",
    "pk_keyword",
    "pk_kwd",
    "pk_medium",
    "pk_source",
    "pl_gc",
    "redirect_log_mongo_id",
    "redirect_mongo_id",
    "rtbhc",
    "rtid",
    "s_cid",
    "s_kwcid",
    "sb_referer_host",
    "scadid",
    "srsltid",
    "trk_contact",
    "trk_module",
    "trk_msg",
    "trk_sid",
    "ttadid",
    "ttclid",
    "tw_adid",
    "tw_campaign",
    "tw_content",
    "tw_kwdid",
    "tw_source",
    "tw_term",
    "twclid",
    "utm_ad",
    "utm_affiliate",
    "utm_brand",
    "utm_campaign",
    "utm_campaignid",
    "utm_channel",
    "utm_cid",
    "utm_content",
    "utm_creative",
    "utm_creative_format",
    "utm_emcid",
    "utm_emmid",
    "utm_id",
    "utm_id_",
    "utm_keyword",
    "utm_klaviyo_id",
    "utm_marketing_tactic",
    "utm_medium",
    "utm_name",
    "utm_place",
    "utm_product",
    "utm_pubreferrer",
    "utm_reader",
    "utm_referrer",
    "utm_serial",
    "utm_session",
    "utm_siteid",
    "utm_social",
    "utm_social-type",
    "utm_source",
    "utm_source_platform",
    "utm_supplier",
    "utm_swu",
    "utm_term",
    "utm_umguk",
    "utm_userid",
    "utm_viz_id",
    "var",
    "vero_conv",
    "wbraid",
    "wt_mc",
    "yclid",
    "ymid",
});

constexpr std::string_view kTwitterParams[] = {
    "ref_src",
    "ref_url",
};
constexpr std::string_view kTwitterSites[] = {
    "twitter.com",
    "x.com",
};

constexpr std::string_view kInstagramParams[] = {
    "igshid",
    "ig_rid",
};
constexpr std::string_view kInstagramSites[] = {
    "instagram.com",
};

constexpr std::string_view kAmazonParams[] = {
    "__mk_de_DE",
    "aaxitk",
    "content-id",
    "creativeId",
    "crid",
    "dib",
    "dib_tag",
    "eac_id",
    "eac_link",
    "eac_selected",
    "hsa_cr_id",
    "ipRedirectFrom",
    "ipRedirectOriginalURL",
    "keywords",
    "lp_asins",
    "lp_query",
    "lp_slot",
    "overrideBaseCountry",
    "pageLoadId",
    "pd_rd_plhdr",
    "pd_rd_r",
    "pd_rd_w",
    "pd_rd_wg",
    "pf_rd_r",
    "pf_rd_p",
    "plink",
    "psc",
    "ref",
    "ref_",
    "ref_pageloadid",
    "sbo",
    "social_share",
    "source_code",
    "sprefix",
    "s_prefix",
    "sp_csd",
    "sr",
    "store_ref",
};
constexpr std::string_view kAmazonSites[] = {
    "amazon.com",    "amazon.de",     "amazon.nl",      "amazon.fr",
    "amazon.co.jp",  "amazon.in",     "amazon.es",      "amazon.ac",
    "amazon.cn",     "amazon.eg",     "amazon.co.uk",   "amazon.it",
    "amazon.pl",     "amazon.sg",     "amazon.ca",      "amazon.com.au",
    "amazon.com.br", "amazon.com.mx", "amazon.com.tr",  "amazon.sa",
    "amazon.ae",     "amazon.se",     "amazon.ie",      "amazon.co.za",
    "audible.com",   "audible.co.uk", "audible.de",     "audible.fr",
    "audible.it",    "audible.ca",    "audible.com.au", "audible.co.jp",
    "audible.in",    "audible.es",    "audible.com.br",
};
constexpr std::string_view kAmazonHosts[] = {
    "www.amazon.com.be",
};

constexpr std::string_view kHandelsblattParams[] = {
    "share",
};
constexpr std::string_view kHandelsblattSites[] = {
    "handelsblatt.com",
};

constexpr std::string_view kShopeeParams[] = {
    "sp_atk",
    "xptdk",
};
constexpr std::string_view kShopeeSites[] = {
    "shopee.co.id",  "shopee.tw", "shopee.vn",     "shopee.ph",
    "shopee.com.my", "shopee.sg", "shopee.com.br", "shopee.com.mx",
    "shopee.com.co", "shopee.cl",
};

constexpr std::string_view kTokopediaParams[] = {
    "extParam",
};
constexpr std::string_view kTokopediaSites[] = {
    "tokopedia.com",
};

constexpr std::string_view kSpotifyParams[] = {
    "si",
};
constexpr std::string_view kSpotifySites[] = {
    "spotify.com",
};

constexpr std::string_view kMeetupParams[] = {
    "eventOrigin",
    "recId",
    "recSource",
    "searchId",
};
constexpr std::string_view kMeetupSites[] = {
    "meetup.com",
};

constexpr std::string_view kFacebookParams[] = {
    "__cft__[0]",
    "__cft__%5B0%5D",
    "__tn__",
};
constexpr std::string_view kFacebookSites[] = {
    "facebook.com",
};

constexpr std::string_view kWikimediaParams[] = {
    "wprov",
};
constexpr std::string_view kWikimediaSites[] = {
    "wikipedia.org", "wikimedia.org", "mediawiki.org",
    "w.wiki",        "wikidata.org",  "wikifunctions.org",
};

constexpr std::string_view kBilibiliParams[] = {
    "spm_id_from",
    "broadcast_type",
    "is_room_feed",
    "live_from",
};
constexpr std::string_view kBilibiliSites[] = {
    "bilibili.com",
};

constexpr std::string_view kHumblebundleParams[] = {
    "hmb_source",
    "hmb_medium",
    "hmb_campaign",
};
constexpr std::string_view kHumblebundleSites[] = {
    "humblebundle.com",
};

constexpr std::string_view kLinkedinParams[] = {
    "rcm", "eBP", "alternateChannel", "origin", "lipi", "isJobSearch",
};
constexpr std::string_view kLinkedinSites[] = {
    "linkedin.com",
};

constexpr std::string_view kEbayParams[] = {
    "customid",
    "toolid",
};
constexpr std::string_view kEbaySites[] = {
    "ebay.at",     "ebay.ba",     "ebay.com",    "ebay.com.au", "ebay.com.hk",
    "ebay.com.my", "ebay.com.ph", "ebay.com.sg", "ebay.com.tw", "ebay.co.uk",
    "ebay.co.jp",  "ebay.co.kr",  "ebay.de",     "ebay.fr",     "ebay.it",
    "ebay.es",     "ebay.ca",     "ebay.nl",     "ebay.be",     "ebay.ch",
    "ebay.cn",     "ebay.ie",     "ebay.pl",
};

constexpr SiteRule kSiteRules[] = {
    {kTwitterParams, kTwitterSites, {}},
    {kInstagramParams, kInstagramSites, {}},
    {kAmazonParams, kAmazonSites, kAmazonHosts},
    {kHandelsblattParams, kHandelsblattSites, {}},
    {kShopeeParams, kShopeeSites, {}},
    {kTokopediaParams, kTokopediaSites, {}},
    {kSpotifyParams, kSpotifySites, {}},
    {kMeetupParams, kMeetupSites, {}},
    {kFacebookParams, kFacebookSites, {}},
    {kWikimediaParams, kWikimediaSites, {}},
    {kBilibiliParams, kBilibiliSites, {}},
    {kHumblebundleParams, kHumblebundleSites, {}},
    {kLinkedinParams, kLinkedinSites, {}},
    {kEbayParams, kEbaySites, {}},
};

bool IsOnSite(std::string_view host, std::string_view site) {
  return host == site || (host.size() > site.size() && host.ends_with(site) &&
                          host[host.size() - site.size() - 1] == '.');
}

bool IsSiteParam(std::string_view host, std::string_view name) {
  for (const SiteRule& rule : kSiteRules) {
    bool applies = false;
    for (std::string_view site : rule.sites) {
      applies |= IsOnSite(host, site);
    }
    for (std::string_view exact : rule.hosts) {
      applies |= host == exact;
    }
    if (!applies) {
      continue;
    }
    for (std::string_view param : rule.params) {
      if (name == param) {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

GURL StripTrackingParameters(const GURL& url) {
  if (!url.SchemeIsHTTPOrHTTPS() || !url.has_query()) {
    return url;
  }
  const std::string host = base::ToLowerASCII(url.host());
  std::vector<std::string_view> kept;
  bool stripped = false;
  // The names as they are written (Firefox compares them so: its list has
  // both "__cft__[0]" and "__cft__%5B0%5D").
  for (std::string_view pair : base::SplitStringPiece(
           url.query(), "&", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
    const std::string_view name = pair.substr(0, pair.find('='));
    if (!name.empty() &&
        (kGlobalParams.contains(name) || IsSiteParam(host, name))) {
      stripped = true;
      continue;
    }
    kept.push_back(pair);
  }
  if (!stripped) {
    return url;
  }
  const std::string query = base::JoinString(kept, "&");
  GURL::Replacements replacements;
  if (query.empty()) {
    replacements.ClearQuery();
  } else {
    replacements.SetQueryStr(query);
  }
  return url.ReplaceComponents(replacements);
}

}  // namespace koan
