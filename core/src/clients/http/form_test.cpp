#include <clients/http/form.hpp>

#include <clients/http/client.hpp>
#include <utest/simple_server.hpp>
#include <utest/utest.hpp>

namespace {

constexpr std::size_t kHttpIoThreads{1};

using HttpResponse = testing::SimpleServer::Response;
using HttpRequest = testing::SimpleServer::Request;

constexpr char kTestData[] = "Test Data";
constexpr char kOtherTestData[] = "Other test data";

constexpr char kKey[] = "key";
constexpr char kKey2[] = "key2";

constexpr char kImageJpeg[] = "image/jpeg";
constexpr char kImageBmp[] = "image/bmp";

constexpr char kFileNameTxt[] = "file_name.txt";
constexpr char kFileName2Bmp[] = "file_name2.bmp";

constexpr char kOkCloseResponse[] =
    "HTTP/1.1 200 OK\r\nConnection: close\r\rContent-Length: "
    "0\r\n\r\n";

bool ReceivedFull(const HttpRequest& request) {
  constexpr char kBoundary[] = "boundary=";
  auto pos = request.find(kBoundary);
  if (pos == std::string::npos) {
    return false;
  }

  pos += sizeof(kBoundary) - 1;
  auto end_pos = request.find("\r\n", pos);
  if (end_pos == std::string::npos) {
    return false;
  }

  std::string end_boundary = request.substr(pos, end_pos - pos);
  end_boundary += "--";
  return request.find(end_boundary) != std::string::npos;
}

void validate_filesend(const HttpRequest& request, std::string key,
                       std::string filename, const std::string& content_type,
                       const std::string& test_data) {
  key = "name=\"" + key + "\"";
  filename = "filename=\"" + filename + "\"";

  const auto key_pos = request.find(key);
  EXPECT_NE(key_pos, std::string::npos)
      << "Found no '" << key << "' in: " << request;

  const auto test_data_pos = request.find(test_data);
  EXPECT_NE(test_data_pos, std::string::npos)
      << "failed to find '" << test_data << "' in:" << request;

  const auto multipart_pos = request.find("multipart/form-data");
  EXPECT_NE(multipart_pos, std::string::npos)
      << "failed to find 'multipart/form-data' in:" << request;

  const auto filename_pos = request.find(filename);
  EXPECT_NE(filename_pos, std::string::npos)
      << "failed to find '" << filename << "' in:" << request;

  const auto content_type_pos = request.find(content_type);
  EXPECT_NE(content_type_pos, std::string::npos)
      << "failed to find 'image/jpeg' in:" << request;

  EXPECT_LT(multipart_pos, filename_pos)
      << "Filename apperas before 'multipart/form-data': " << request;

  EXPECT_LT(multipart_pos, content_type_pos)
      << "Nested content type appears before 'multipart/form-data': "
      << request;

  EXPECT_LT(content_type_pos, test_data_pos)
      << "Nested content type appears after test data: " << request;

  EXPECT_LT(filename_pos, test_data_pos)
      << "Nested filename appears after test data: " << request;
}

HttpResponse validating_callback1(const HttpRequest& request) {
  if (!ReceivedFull(request)) {
    return {{}, HttpResponse::kTryReadMore};
  }
  validate_filesend(request, kKey, kFileNameTxt, kImageJpeg, kTestData);

  return {kOkCloseResponse, HttpResponse::kWriteAndClose};
}

HttpResponse validating_callback2(const HttpRequest& request) {
  if (!ReceivedFull(request)) {
    return {{}, HttpResponse::kTryReadMore};
  }

  validate_filesend(request, kKey, kFileNameTxt, kImageJpeg, kTestData);
  validate_filesend(request, kKey2, kFileName2Bmp, kImageBmp, kOtherTestData);

  return {kOkCloseResponse, HttpResponse::kWriteAndClose};
}

}  // namespace

TEST(CurlFormTest, MultipartFileWithContentType) {
  TestInCoro([] {
    const testing::SimpleServer http_server({51234}, &validating_callback1);

    std::shared_ptr<clients::http::Client> http_client_ptr =
        clients::http::Client::Create(kHttpIoThreads);

    auto form = std::make_shared<clients::http::Form>();
    form->add_buffer(kKey, kFileNameTxt,
                     std::make_shared<std::string>(kTestData), kImageJpeg);

    auto resp = http_client_ptr->CreateRequest()
                    ->post("http://127.0.0.1:51234/", form)
                    ->retry(1)
                    ->verify(true)
                    ->http_version(curl::easy::http_version_1_1)
                    ->timeout(std::chrono::milliseconds(100))
                    ->perform();

    EXPECT_EQ(resp->status_code(), clients::http::Status::OK);
  });
}

TEST(CurlFormTest, FilesWithContentType) {
  TestInCoro([] {
    const testing::SimpleServer http_server({51234}, &validating_callback2);

    std::shared_ptr<clients::http::Client> http_client_ptr =
        clients::http::Client::Create(kHttpIoThreads);

    auto form = std::make_shared<clients::http::Form>();
    form->add_buffer(kKey, kFileNameTxt,
                     std::make_shared<std::string>(kTestData), kImageJpeg);

    form->add_buffer(kKey2, kFileName2Bmp,
                     std::make_shared<std::string>(kOtherTestData), kImageBmp);

    auto resp = http_client_ptr->CreateRequest()
                    ->post("http://127.0.0.1:51234/", form)
                    ->retry(1)
                    ->verify(true)
                    ->http_version(curl::easy::http_version_1_1)
                    ->timeout(std::chrono::milliseconds(100))
                    ->perform();

    EXPECT_EQ(resp->status_code(), clients::http::Status::OK);
  });
}