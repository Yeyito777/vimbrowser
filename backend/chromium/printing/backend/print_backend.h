// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PRINTING_BACKEND_PRINT_BACKEND_H_
#define PRINTING_BACKEND_PRINT_BACKEND_H_

#include <stdint.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "base/component_export.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "build/build_config.h"
#include "printing/mojom/print.mojom.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"


// This is the interface for platform-specific code for a print backend
namespace printing {

using PrinterBasicInfoOptions = std::map<std::string, std::string>;

struct COMPONENT_EXPORT(PRINT_BACKEND) PrinterBasicInfo {
  PrinterBasicInfo();
  PrinterBasicInfo(const std::string& printer_name,
                   const std::string& display_name,
                   const std::string& printer_description,
                   const PrinterBasicInfoOptions& options);
  PrinterBasicInfo(const PrinterBasicInfo& other);
  ~PrinterBasicInfo();

  bool operator==(const PrinterBasicInfo& other) const;

  // The name of the printer as understood by OS.
  std::string printer_name;

  // The name of the printer as shown in Print Preview.
  // For Windows SetGetDisplayNameFunction() can be used to set the setter of
  // this field.
  std::string display_name;
  std::string printer_description;
  PrinterBasicInfoOptions options;
};

using PrinterList = std::vector<PrinterBasicInfo>;



struct COMPONENT_EXPORT(PRINT_BACKEND) PrinterSemanticCapsAndDefaults {
  PrinterSemanticCapsAndDefaults();
  PrinterSemanticCapsAndDefaults(const PrinterSemanticCapsAndDefaults& other);
  ~PrinterSemanticCapsAndDefaults();

  bool collate_capable = false;
  bool collate_default = false;

  // If `copies_max` > 1, copies are supported.
  // If `copies_max` = 1, copies are not supported.
  // `copies_max` should never be < 1.
  int32_t copies_max = 1;

  std::vector<mojom::DuplexMode> duplex_modes;
  mojom::DuplexMode duplex_default = mojom::DuplexMode::kUnknownDuplexMode;

  bool color_changeable = false;
  bool color_default = false;
  mojom::ColorModel color_model = mojom::ColorModel::kUnknownColorModel;
  mojom::ColorModel bw_model = mojom::ColorModel::kUnknownColorModel;

  class COMPONENT_EXPORT(PRINT_BACKEND) Paper {
   public:
    Paper();
    Paper(const std::string& display_name,
          const std::string& vendor_id,
          const gfx::Size& size_um);
    Paper(const std::string& display_name,
          const std::string& vendor_id,
          const gfx::Size& size_um,
          const gfx::Rect& printable_area_um);
    Paper(const std::string& display_name,
          const std::string& vendor_id,
          const gfx::Size& size_um,
          const gfx::Rect& printable_area_um,
          int max_height_um);
    Paper(const std::string& display_name,
          const std::string& vendor_id,
          const gfx::Size& size_um,
          const gfx::Rect& printable_area_um,
          int max_height_um,
          bool has_borderless_variant
    );

    ~Paper();

    // The compiler has decided that this class is now "complex" and thus
    // requires an explicit, out-of-line copy constructor.
    Paper(const Paper& other);
    Paper& operator=(const Paper& other);

    bool operator==(const Paper& other) const;

    const std::string& display_name() const { return display_name_; }
    const std::string& vendor_id() const { return vendor_id_; }
    const gfx::Size& size_um() const { return size_um_; }
    const gfx::Rect& printable_area_um() const { return printable_area_um_; }
    int max_height_um() const { return max_height_um_; }
    bool has_borderless_variant() const { return has_borderless_variant_; }

    void set_display_name(const std::string& display_name) {
      display_name_ = display_name;
    }
    void set_vendor_id(const std::string& vendor_id) { vendor_id_ = vendor_id; }
    void set_has_borderless_variant(bool has_borderless_variant) {
      has_borderless_variant_ = has_borderless_variant;
    }

    void set_printable_area_to_paper_size() {
      printable_area_um_ = gfx::Rect(size_um_);
    }

    bool SupportsCustomSize() const;
    // Return true if `other_um` is the same size as this object or if this
    // object supports a custom size and `other_um` falls within the custom size
    // of this object.  Else, return false.
    bool IsSizeWithinBounds(const gfx::Size& other_um) const;


   private:
    std::string display_name_;
    std::string vendor_id_;
    gfx::Size size_um_;

    // Origin (x,y) is at the bottom-left.
    gfx::Rect printable_area_um_;

    // This is used to represent a printer that supports a variable height.
    // This will either be equal to 0 (which indicates the height is not
    // variable) or this will be larger than the height in `size_um` (which
    // indicates the height can be anywhere in that range).  Note that
    // `printable_area_um` is always based on `size_um`.
    int max_height_um_ = 0;

    // True if this paper size can be used borderless (with the printable area
    // covering the entire page) in addition to bordered. If a paper size
    // *only* supports borderless and has no variant with margins, this field
    // will be false and `printable_area_um` will cover the entire page.
    bool has_borderless_variant_ = false;

  };
  using Papers = std::vector<Paper>;
  Papers papers;
  Papers user_defined_papers;
  Paper default_paper;

  // Describes a media type (plain paper, photo paper, etc.)
  // TODO(crbug.com/40274266): Support media types on platforms other than
  // ChromeOS
  struct COMPONENT_EXPORT(PRINT_BACKEND) MediaType {
    std::string display_name;
    std::string vendor_id;

    bool operator==(const MediaType& other) const;
  };
  using MediaTypes = std::vector<MediaType>;
  MediaTypes media_types;
  MediaType default_media_type;

  std::vector<gfx::Size> dpis;
  gfx::Size default_dpi;


};

#if defined(UNIT_TEST)

COMPONENT_EXPORT(PRINT_BACKEND)
bool operator==(const PrinterSemanticCapsAndDefaults& caps1,
                const PrinterSemanticCapsAndDefaults& caps2);

#endif  // defined(UNIT_TEST)

struct COMPONENT_EXPORT(PRINT_BACKEND) PrinterCapsAndDefaults {
  PrinterCapsAndDefaults();
  PrinterCapsAndDefaults(const PrinterCapsAndDefaults& other);
  ~PrinterCapsAndDefaults();

  std::string printer_capabilities;
  std::string caps_mime_type;
  std::string printer_defaults;
  std::string defaults_mime_type;
};

// PrintBackend class will provide interface for different print backends
// (Windows, CUPS) to implement. User will call CreateInstance() to
// obtain available print backend.
// Please note, that PrintBackend is not platform specific, but rather
// print system specific. For example, CUPS is available on both Linux and Mac,
// but not available on ChromeOS, etc. This design allows us to add more
// functionality on some platforms, while reusing core (CUPS) functions.
class COMPONENT_EXPORT(PRINT_BACKEND) PrintBackend
    : public base::RefCountedThreadSafe<PrintBackend> {
 public:
  // Enumerates the list of installed local and network printers.  It will
  // return success when the available installed printers have been enumerated
  // into `printer_list`.  Note that `printer_list` should be empty prior to
  // this call.  If there are no printers installed then it will still return
  // success, and `printer_list` remains empty.  The result code will return
  // one of the error result codes when there is a failure in generating the
  // list.
  virtual mojom::ResultCode EnumeratePrinters(PrinterList& printer_list) = 0;

  // Gets the default printer name.  If there is no default printer then it
  // will still return success and `default_printer` will be empty.  The result
  // code will return one of the error result codes when there is a failure in
  // trying to get the default printer.
  virtual mojom::ResultCode GetDefaultPrinterName(
      std::string& default_printer) = 0;

  // Gets the basic printer info for a specific printer. Implementations must
  // check `printer_name` validity in the same way as IsValidPrinter().
  virtual mojom::ResultCode GetPrinterBasicInfo(
      const std::string& printer_name,
      PrinterBasicInfo* printer_info) = 0;

  // Gets the semantic capabilities and defaults for a specific printer.
  // This is usually a lighter implementation than GetPrinterCapsAndDefaults().
  // Implementations must check `printer_name` validity in the same way as
  // IsValidPrinter().
  // NOTE: on some old platforms (WinXP without XPS pack)
  // GetPrinterCapsAndDefaults() will fail, while this function will succeed.
  virtual mojom::ResultCode GetPrinterSemanticCapsAndDefaults(
      const std::string& printer_name,
      PrinterSemanticCapsAndDefaults* printer_info) = 0;


  // Gets the information about driver for a specific printer.  A maximum of
  // 4 elements can be in the returned result, due to limitations on how this
  // is intended to be used for crash keys by `ScopedPrinterInfo`.
  virtual std::vector<std::string> GetPrinterDriverInfo(
      const std::string& printer_name) = 0;

  // Returns true if printer_name points to a valid printer.
  virtual bool IsValidPrinter(const std::string& printer_name) = 0;


  // Allocates a print backend.
  static scoped_refptr<PrintBackend> CreateInstance(const std::string& locale);

  // Test method to override the print backend for testing.  Caller should
  // retain ownership.
  static void SetPrintBackendForTesting(PrintBackend* print_backend);

 protected:
  friend class base::RefCountedThreadSafe<PrintBackend>;

  PrintBackend();
  virtual ~PrintBackend();

  // Provide the actual backend for CreateInstance().
  static scoped_refptr<PrintBackend> CreateInstanceImpl(
      const std::string& locale);
};

}  // namespace printing

#endif  // PRINTING_BACKEND_PRINT_BACKEND_H_
