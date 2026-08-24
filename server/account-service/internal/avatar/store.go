package avatar

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"fmt"
	"image"
	_ "image/jpeg"
	_ "image/png"
	"net/http"
	"strings"
	"time"

	"github.com/aws/aws-sdk-go-v2/aws"
	awsconfig "github.com/aws/aws-sdk-go-v2/config"
	"github.com/aws/aws-sdk-go-v2/service/s3"
)

const (
	MaxBytes      = 5 * 1024 * 1024
	MaxDimension  = 4096
	MinDimension  = 32
	DefaultURLTTL = 10 * time.Minute
)

var ErrDisabled = errors.New("avatar storage disabled")

type Store interface {
	Put(context.Context, string, []byte) (string, error)
	Delete(context.Context, string) error
	PresignGet(context.Context, string, time.Duration) (string, error)
}

type DisabledStore struct{}

func (DisabledStore) Put(context.Context, string, []byte) (string, error) {
	return "", ErrDisabled
}

func (DisabledStore) Delete(context.Context, string) error {
	return ErrDisabled
}

func (DisabledStore) PresignGet(context.Context, string, time.Duration) (string, error) {
	return "", ErrDisabled
}

type TigrisStore struct {
	client    *s3.Client
	presigner *s3.PresignClient
	bucket    string
}

func NewTigrisStore(ctx context.Context, endpoint, region, bucket string) (*TigrisStore, error) {
	endpoint = strings.TrimSpace(endpoint)
	region = strings.TrimSpace(region)
	bucket = strings.TrimSpace(bucket)
	if endpoint == "" || region == "" || bucket == "" {
		return nil, fmt.Errorf("avatar storage configuration is incomplete")
	}

	cfg, err := awsconfig.LoadDefaultConfig(ctx, awsconfig.WithRegion(region))
	if err != nil {
		return nil, fmt.Errorf("load avatar storage configuration: %w", err)
	}

	client := s3.NewFromConfig(cfg, func(options *s3.Options) {
		options.BaseEndpoint = aws.String(endpoint)
	})
	return &TigrisStore{
		client:    client,
		presigner: s3.NewPresignClient(client),
		bucket:    bucket,
	}, nil
}

func (s *TigrisStore) Put(ctx context.Context, accountID string, data []byte) (string, error) {
	contentType, extension, err := Validate(data)
	if err != nil {
		return "", err
	}

	random := make([]byte, 16)
	if _, err := rand.Read(random); err != nil {
		return "", fmt.Errorf("generate avatar object key: %w", err)
	}
	key := fmt.Sprintf("avatars/%s/%s%s",
		strings.TrimSpace(accountID),
		hex.EncodeToString(random),
		extension)

	_, err = s.client.PutObject(ctx, &s3.PutObjectInput{
		Bucket:       aws.String(s.bucket),
		Key:          aws.String(key),
		Body:         bytes.NewReader(data),
		ContentType:  aws.String(contentType),
		CacheControl: aws.String("private, max-age=300"),
	})
	if err != nil {
		return "", fmt.Errorf("store avatar: %w", err)
	}
	return key, nil
}

func (s *TigrisStore) Delete(ctx context.Context, key string) error {
	if strings.TrimSpace(key) == "" {
		return nil
	}
	if _, err := s.client.DeleteObject(ctx, &s3.DeleteObjectInput{
		Bucket: aws.String(s.bucket),
		Key:    aws.String(key),
	}); err != nil {
		return fmt.Errorf("delete avatar: %w", err)
	}
	return nil
}

func (s *TigrisStore) PresignGet(ctx context.Context, key string, ttl time.Duration) (string, error) {
	if strings.TrimSpace(key) == "" {
		return "", nil
	}
	if ttl <= 0 {
		ttl = DefaultURLTTL
	}

	request, err := s.presigner.PresignGetObject(
		ctx,
		&s3.GetObjectInput{
			Bucket: aws.String(s.bucket),
			Key:    aws.String(key),
		},
		func(options *s3.PresignOptions) {
			options.Expires = ttl
		})
	if err != nil {
		return "", fmt.Errorf("presign avatar: %w", err)
	}
	return request.URL, nil
}

func Validate(data []byte) (contentType string, extension string, err error) {
	if len(data) == 0 || len(data) > MaxBytes {
		return "", "", fmt.Errorf("avatar size is outside the supported range")
	}

	contentType = http.DetectContentType(data)
	switch contentType {
	case "image/jpeg":
		extension = ".jpg"
	case "image/png":
		extension = ".png"
	default:
		return "", "", fmt.Errorf("avatar format is unsupported")
	}

	config, _, err := image.DecodeConfig(bytes.NewReader(data))
	if err != nil {
		return "", "", fmt.Errorf("decode avatar metadata: %w", err)
	}
	if config.Width < MinDimension || config.Height < MinDimension ||
		config.Width > MaxDimension || config.Height > MaxDimension {
		return "", "", fmt.Errorf("avatar dimensions are outside the supported range")
	}
	return contentType, extension, nil
}
